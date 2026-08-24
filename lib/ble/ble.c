// we do GAP instead of GATT (gatt is for full conecctions)
#include "esp_err.h"
#include "nimble/nimble_port.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <esp_log.h>
static const char *TAG = "BLE SPAMMA";

void init_nvs_flash_w_nimble(void) {
  esp_err_t error = nvs_flash_init();
  if (error == ESP_ERR_NVS_NO_FREE_PAGES) {
    error = ESP_ERR_NVS_NEW_VERSION_FOUND;
    ESP_ERROR_CHECK(nvs_flash_erase());
    error = nvs_flash_init();
  }

  if (error != ESP_OK) {
    ESP_LOGI(TAG, "failed to initalize nvs flash, error code: %d", error);
    return;
  }

  error = nimble_port_init();
  if (error != ESP_OK) {
    ESP_LOGI(TAG, "failed to initialize nimble stack, error code: %d ", error);
    return;
  }

  // error = gap_init();
  // if (error != 0) {
  //     ESP_LOGE(TAG, "failed to initialize GAP service, error code: %d",
  //              error);
  //     return;
  // }
}
