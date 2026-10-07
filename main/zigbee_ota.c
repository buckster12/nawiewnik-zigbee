#include "zigbee_ota.h"
#include "ota_stream.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "motor_driver.h"
#include "nawiewnik.h"
static const esp_partition_t *partition;
static esp_ota_handle_t handle;
static bool active, checked, applied;
static ota_stream_t stream;
static esp_timer_handle_t restart_timer;
static uint32_t current_version=NAW_OTA_VERSION;
static void restart_cb(void *arg) { (void)arg; motor_driver_stop(); esp_restart(); }
static void abort_download(void) {
    if (active) esp_ota_abort(handle);
    active=false; checked=false;
}
static bool acceptable(uint16_t manufacturer, uint16_t type, uint32_t version, uint32_t size) {
    const esp_partition_t *next=esp_ota_get_next_update_partition(NULL);
    return !applied && manufacturer==NAW_OTA_MANUFACTURER && type==NAW_OTA_IMAGE_TYPE &&
        version>NAW_OTA_VERSION && next && next!=esp_ota_get_running_partition() &&
        size>62 && size-62<=next->size;
}
static int write_payload(const uint8_t *data, unsigned size, void *ctx) {
    (void)ctx; return esp_ota_write(handle,data,size)==ESP_OK ? 0 : -1;
}
void zigbee_ota_query(ezb_zcl_ota_upgrade_query_next_image_rsp_message_t *m) {
    if (!m) return;
    m->out.result = m->in.image.status!=EZB_ZCL_OTA_UPGRADE_STATUS_CODE_SUCCESS ||
        acceptable(m->in.image.manuf_code,m->in.image.image_type,m->in.image.file_version,m->in.image.size)
        ? EZB_ZCL_STATUS_SUCCESS : EZB_ZCL_STATUS_INVALID_IMAGE;
}
void zigbee_ota_progress(ezb_zcl_ota_upgrade_client_progress_message_t *m) {
    if (!m) return;
    esp_err_t err=ESP_ERR_INVALID_STATE;
    switch (m->in.progress) {
    case EZB_ZCL_OTA_UPGRADE_PROGRESS_START:
        abort_download();
        if (!acceptable(m->in.start.manuf_code,m->in.start.image_type,m->in.start.file_version,m->in.start.image_size)) break;
        partition=esp_ota_get_next_update_partition(NULL);
        ota_stream_init(&stream,m->in.start.image_size,partition->size,m->in.start.file_version);
        err=esp_ota_begin(partition,OTA_WITH_SEQUENTIAL_WRITES,&handle);
        active=err==ESP_OK;
        break;
    case EZB_ZCL_OTA_UPGRADE_PROGRESS_RECEIVING:
        if (active && !checked) err=ota_stream_feed(&stream,m->in.receiving.file_offset,
            m->in.receiving.block,m->in.receiving.block_size,write_payload,NULL)==0 ? ESP_OK : ESP_FAIL;
        break;
    case EZB_ZCL_OTA_UPGRADE_PROGRESS_CHECK:
        if (active && ota_stream_complete(&stream) && m->in.check.manuf_code==NAW_OTA_MANUFACTURER &&
            m->in.check.image_type==NAW_OTA_IMAGE_TYPE && m->in.check.file_version==stream.version) {
            /* IDF validates the complete ESP image (chip ID/checksum/SHA) before boot selection. */
            err=esp_ota_end(handle); active=false; checked=err==ESP_OK;
        }
        break;
    case EZB_ZCL_OTA_UPGRADE_PROGRESS_APPLY:
        if (checked && !applied && m->in.apply.manuf_code==NAW_OTA_MANUFACTURER &&
            m->in.apply.image_type==NAW_OTA_IMAGE_TYPE && m->in.apply.file_version==stream.version) {
            err=esp_ota_set_boot_partition(partition); applied=err==ESP_OK;
        }
        break;
    case EZB_ZCL_OTA_UPGRADE_PROGRESS_FINISH:
        if (applied) {
            esp_timer_create_args_t args={.callback=restart_cb,.name="ota_reboot"};
            err=restart_timer ? ESP_ERR_INVALID_STATE : esp_timer_create(&args,&restart_timer);
            if (err==ESP_OK) err=esp_timer_start_once(restart_timer,
                ((uint64_t)m->in.finish.count_down_delay+1)*1000000ULL);
        }
        break;
    case EZB_ZCL_OTA_UPGRADE_PROGRESS_ABORT:
        abort_download(); err=ESP_OK; break;
    default: break;
    }
    if (err!=ESP_OK) { abort_download(); ESP_LOGW("ota","Rejected progress %d: %s",m->in.progress,esp_err_to_name(err)); }
    m->out.result=err==ESP_OK ? EZB_ZCL_STATUS_SUCCESS : EZB_ZCL_STATUS_INVALID_IMAGE;
}
esp_err_t zigbee_ota_add_cluster(ezb_af_ep_desc_t endpoint) {
    ezb_zcl_ota_upgrade_cluster_client_config_t cfg={
        .upgrade_server_id=EZB_ZCL_OTA_UPGRADE_UPGRADE_SERVER_ID_DEFAULT_VALUE,
        .file_offset=0,.image_upgrade_status=0,
        .manufacturer_id=NAW_OTA_MANUFACTURER,.image_type_id=NAW_OTA_IMAGE_TYPE};
    ezb_zcl_cluster_desc_t desc=ezb_zcl_ota_upgrade_create_cluster_desc(&cfg,EZB_ZCL_CLUSTER_CLIENT);
    if (!desc) return ESP_FAIL;
    esp_err_t err=ezb_zcl_ota_upgrade_cluster_desc_add_attr(desc,
        EZB_ZCL_ATTR_OTA_UPGRADE_CURRENT_FILE_VERSION_ID,&current_version);
    if (err!=ESP_OK) return err;
    return ezb_af_endpoint_add_cluster_desc(endpoint,desc);
}
