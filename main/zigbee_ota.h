#pragma once
#include "esp_err.h"
#include "ezbee/zcl/cluster/ota_upgrade.h"
esp_err_t zigbee_ota_add_cluster(ezb_af_ep_desc_t endpoint);
void zigbee_ota_progress(ezb_zcl_ota_upgrade_client_progress_message_t *message);
void zigbee_ota_query(ezb_zcl_ota_upgrade_query_next_image_rsp_message_t *message);
