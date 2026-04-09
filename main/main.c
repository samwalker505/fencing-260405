#include "driver/gpio.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_common_api.h"
#include "esp_gatts_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <stdbool.h>
#include <string.h>

#define TAG "BTN_BLE"

#define LED_G_PIN GPIO_NUM_4
#define BUZZER_PIN GPIO_NUM_5
#define BUTTON_PIN GPIO_NUM_18

#define GATTS_APP_ID 0x55
#define PROFILE_NUM 1
#define PROFILE_APP_IDX 0

#define SVC_UUID 0x00FF
#define CHAR_UUID 0xFF01
#define DEV_NAME "FencingButton"

enum {
  IDX_SVC,
  IDX_CHAR_DECL,
  IDX_CHAR_VAL,
  IDX_CHAR_CFG,
  HRS_IDX_NB,
};

static uint16_t gatt_db_handle_table[HRS_IDX_NB];
static esp_gatt_if_t s_gatts_if = 0;
static uint16_t s_conn_id = 0;
static bool s_connected = false;
static bool s_notify_enabled = false;
static uint8_t s_button_value = 0;

static uint8_t adv_service_uuid128[16] = {
    0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = false,
    .min_interval = 0x20,
    .max_interval = 0x40,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(adv_service_uuid128),
    .p_service_uuid = adv_service_uuid128,
    .flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT,
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid =
    ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static const uint16_t service_uuid = SVC_UUID;
static const uint16_t button_char_uuid = CHAR_UUID;
static const uint8_t char_prop_read_notify =
    ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static uint8_t char_value = 0;
static uint8_t cccd[2] = {0x00, 0x00};

static const esp_gatts_attr_db_t gatt_db[HRS_IDX_NB] = {
    [IDX_SVC] = {{ESP_GATT_AUTO_RSP},
                 {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid,
                  ESP_GATT_PERM_READ, sizeof(uint16_t), sizeof(uint16_t),
                  (uint8_t *)&service_uuid}},
    [IDX_CHAR_DECL] = {{ESP_GATT_AUTO_RSP},
                       {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid,
                        ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(uint8_t),
                        (uint8_t *)&char_prop_read_notify}},
    [IDX_CHAR_VAL] = {{ESP_GATT_AUTO_RSP},
                      {ESP_UUID_LEN_16, (uint8_t *)&button_char_uuid,
                       ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(uint8_t),
                       &char_value}},
    [IDX_CHAR_CFG] = {{ESP_GATT_AUTO_RSP},
                      {ESP_UUID_LEN_16,
                       (uint8_t *)&character_client_config_uuid,
                       ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(cccd),
                       sizeof(cccd), cccd}},
};

static void button_init(void) {
  gpio_config_t cfg = {
      .pin_bit_mask = 1ULL << BUTTON_PIN,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&cfg);
}

static void io_init(void) {
  gpio_config_t cfg = {
      .pin_bit_mask = (1ULL << LED_G_PIN) | (1ULL << BUZZER_PIN),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&cfg);
  gpio_set_level(LED_G_PIN, 1);
  gpio_set_level(BUZZER_PIN, 0);
}

static void gap_event_handler(esp_gap_ble_cb_event_t event,
                              esp_ble_gap_cb_param_t *param) {
  if (event == ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT) {
    esp_ble_gap_start_advertising(&adv_params);
  } else if (event == ESP_GAP_BLE_ADV_START_COMPLETE_EVT) {
    if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
      ESP_LOGI(TAG, "Advertising started");
    } else {
      ESP_LOGE(TAG, "Advertising start failed: %d",
               param->adv_start_cmpl.status);
    }
  }
}

static void gatts_event_handler(esp_gatts_cb_event_t event,
                                esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param) {
  switch (event) {
  case ESP_GATTS_REG_EVT:
    s_gatts_if = gatts_if;
    esp_ble_gap_set_device_name(DEV_NAME);
    esp_ble_gap_config_adv_data(&adv_data);
    esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, HRS_IDX_NB, 0);
    break;
  case ESP_GATTS_CREAT_ATTR_TAB_EVT:
    if (param->add_attr_tab.status == ESP_GATT_OK) {
      memcpy(gatt_db_handle_table, param->add_attr_tab.handles,
             sizeof(gatt_db_handle_table));
      esp_ble_gatts_start_service(gatt_db_handle_table[IDX_SVC]);
    }
    break;
  case ESP_GATTS_CONNECT_EVT:
    s_connected = true;
    s_conn_id = param->connect.conn_id;
    s_notify_enabled = false;
    ESP_LOGI(TAG, "BLE connected");
    break;
  case ESP_GATTS_DISCONNECT_EVT:
    s_connected = false;
    s_notify_enabled = false;
    esp_ble_gap_start_advertising(&adv_params);
    ESP_LOGI(TAG, "BLE disconnected, advertising again");
    break;
  case ESP_GATTS_WRITE_EVT:
    if (!param->write.is_prep &&
        param->write.handle == gatt_db_handle_table[IDX_CHAR_CFG] &&
        param->write.len == 2) {
      uint16_t value = param->write.value[1] << 8 | param->write.value[0];
      s_notify_enabled = (value == 0x0001 || value == 0x0002);
      ESP_LOGI(TAG, "Notify %s", s_notify_enabled ? "enabled" : "disabled");
    }
    break;
  default:
    break;
  }
}

static void ble_init(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
  ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
  ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
  ESP_ERROR_CHECK(esp_bluedroid_init());
  ESP_ERROR_CHECK(esp_bluedroid_enable());

  ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_event_handler));
  ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));
  ESP_ERROR_CHECK(esp_ble_gatts_app_register(GATTS_APP_ID));
  ESP_ERROR_CHECK(esp_ble_gatt_set_local_mtu(200));
}

void app_main(void) {
  io_init();
  button_init();
  ble_init();

  int last_button = -1;
  while (1) {
    int button_state = gpio_get_level(BUTTON_PIN);

    // Pull-up input: pressed is typically 0.
    gpio_set_level(LED_G_PIN, button_state ? 1 : 0);
    // gpio_set_level(BUZZER_PIN, button_state ? 0 : 1);

    if (button_state != last_button) {
      last_button = button_state;
      s_button_value = (uint8_t)button_state;
      esp_ble_gatts_set_attr_value(gatt_db_handle_table[IDX_CHAR_VAL],
                                   sizeof(s_button_value), &s_button_value);
      if (s_connected && s_notify_enabled) {
        esp_ble_gatts_send_indicate(
            s_gatts_if, s_conn_id, gatt_db_handle_table[IDX_CHAR_VAL],
            sizeof(s_button_value), &s_button_value, false);
      }
      ESP_LOGI(TAG, "Button state: %d", button_state);
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
