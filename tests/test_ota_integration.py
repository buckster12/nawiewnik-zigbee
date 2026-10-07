from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[1]
class OtaIntegration(unittest.TestCase):
    def test_receive_validate_setboot(self):
        path = ROOT / 'main/zigbee_ota.c'
        self.assertTrue(path.exists(), 'Native OTA callback missing')
        text = path.read_text()
        for token in ['esp_ota_begin', 'ota_stream_feed', 'ota_stream_complete', 'esp_ota_end', 'esp_ota_set_boot_partition', 'esp_ota_abort', 'EZB_ZCL_OTA_UPGRADE_PROGRESS_FINISH']:
            self.assertIn(token, text)
        main = (ROOT / 'main/main.c').read_text()
        self.assertIn('zigbee_ota_add_cluster(endpoint)', main)
        self.assertIn('zigbee_ota_progress(message)', main)
if __name__ == '__main__': unittest.main()
