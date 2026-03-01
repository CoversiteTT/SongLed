#include "ble_service.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdarg.h>

// External function from main.cpp
extern "C" void handleLine(char* line);

static const char* TAG = "BLE_SERVICE";

// BLE connection state
static bool ble_connected = false;
static uint16_t ble_conn_id = 0;
static uint16_t ble_gatts_if = 0;
static ble_connection_callback_t connection_callback = nullptr;
static ble_debug_callback_t debug_callback = nullptr;
static uint8_t adv_config_done = 0;
static bool adv_started = false;
static const char *g_device_name = "SongLed";
static bool cmd_tx_notify_enabled = false;
static bool cmd_rx_seen = false;
static bool ble_initialized = false;
static bool ble_deinitializing = false;
static esp_bd_addr_t remote_bda = {0};
static bool remote_bda_valid = false;
static uint32_t last_conn_param_fix_ms = 0;
static int g_char_count = 0;

constexpr uint8_t ADV_CFG_FLAG_ADV_DATA = (1 << 0);
constexpr uint8_t ADV_CFG_FLAG_SCAN_RSP = (1 << 1);

// Service and characteristic handles
static uint16_t service_handle = 0;
static uint16_t char_cmd_tx_handle = 0;  // ESP32 sends commands
static uint16_t char_cmd_tx_cccd_handle = 0;
static uint16_t char_cmd_rx_handle = 0;  // ESP32 receives responses
static uint16_t char_cover_handle = 0;   // ESP32 receives cover data
// static uint16_t char_status_handle = 0;  // ESP32 sends status notifications (unused currently)

// RX buffer for assembling fragmented messages
#define RX_BUFFER_SIZE 4096
static char rx_buffer[RX_BUFFER_SIZE];
static size_t rx_buffer_pos = 0;

#define BLE_TX_QUEUE_SIZE 12
#define BLE_TX_LINE_MAX 256
static char tx_queue[BLE_TX_QUEUE_SIZE][BLE_TX_LINE_MAX];
static uint8_t tx_head = 0;
static uint8_t tx_tail = 0;
static uint8_t tx_count = 0;

static bool tx_queue_push(const char *line) {
    if (!line || line[0] == '\0') return true;
    if (tx_count >= BLE_TX_QUEUE_SIZE) {
        // Drop oldest to keep newest control messages.
        tx_head = (tx_head + 1) % BLE_TX_QUEUE_SIZE;
        tx_count--;
    }
    strncpy(tx_queue[tx_tail], line, BLE_TX_LINE_MAX - 1);
    tx_queue[tx_tail][BLE_TX_LINE_MAX - 1] = '\0';
    tx_tail = (tx_tail + 1) % BLE_TX_QUEUE_SIZE;
    tx_count++;
    return true;
}

static bool tx_queue_pop(char *out, size_t out_len) {
    if (tx_count == 0 || !out || out_len == 0) return false;
    strncpy(out, tx_queue[tx_head], out_len - 1);
    out[out_len - 1] = '\0';
    tx_head = (tx_head + 1) % BLE_TX_QUEUE_SIZE;
    tx_count--;
    return true;
}

static void emit_debug(const char *fmt, ...) {
    if (!debug_callback || !fmt) return;
    char buf[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    buf[sizeof(buf) - 1] = '\0';
    debug_callback(buf);
}

static bool ble_send_line_now(const char* line) {
    if (!ble_connected || !line || char_cmd_tx_handle == 0 || ble_gatts_if == 0) {
        return false;
    }
    size_t len = strlen(line);
    if (len == 0) return true;
    char buffer[BLE_TX_LINE_MAX];
    if (len >= sizeof(buffer) - 1) {
        len = sizeof(buffer) - 2;
    }
    memcpy(buffer, line, len);
    buffer[len] = '\n';
    buffer[len + 1] = '\0';
    esp_err_t ret = esp_ble_gatts_send_indicate(ble_gatts_if, ble_conn_id,
                                                 char_cmd_tx_handle,
                                                 len + 1, (uint8_t*)buffer, false);
    return (ret == ESP_OK);
}

static void flush_tx_queue() {
    if (!ble_connected || !cmd_tx_notify_enabled) return;
    char line[BLE_TX_LINE_MAX];
    int guard = BLE_TX_QUEUE_SIZE + 4;
    while (guard-- > 0 && tx_queue_pop(line, sizeof(line))) {
        if (!ble_send_line_now(line)) {
            // Push back one line and stop to avoid busy failure loop.
            tx_queue_push(line);
            break;
        }
    }
}

static void reset_runtime_state() {
    ble_connected = false;
    ble_conn_id = 0;
    ble_gatts_if = 0;
    adv_config_done = 0;
    adv_started = false;
    cmd_tx_notify_enabled = false;
    cmd_rx_seen = false;
    service_handle = 0;
    char_cmd_tx_handle = 0;
    char_cmd_tx_cccd_handle = 0;
    char_cmd_rx_handle = 0;
    char_cover_handle = 0;
    rx_buffer_pos = 0;
    memset(rx_buffer, 0, sizeof(rx_buffer));
    memset(remote_bda, 0, sizeof(remote_bda));
    remote_bda_valid = false;
    last_conn_param_fix_ms = 0;
    g_char_count = 0;
    tx_head = 0;
    tx_tail = 0;
    tx_count = 0;
}

// UUID conversion helper (convert string UUID to 128-bit array)
static void uuid_str_to_128bit(const char* uuid_str, uint8_t* uuid_128) {
    // Parse UUID string "12345678-1234-5678-1234-56789abcdef0"
    // ESP32 expects LSB first
    uint8_t temp[16];
    int idx = 0;
    for (int i = 0; i < strlen(uuid_str) && idx < 16; i++) {
        if (uuid_str[i] == '-') continue;
        char hex[3] = {uuid_str[i], uuid_str[i+1], 0};
        temp[idx++] = (uint8_t)strtol(hex, nullptr, 16);
        i++;
    }
    // Reverse for LSB first
    for (int i = 0; i < 16; i++) {
        uuid_128[i] = temp[15 - i];
    }
}

// GAP event handler
static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .peer_addr = {0},
    .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static esp_bt_uuid_t cccd_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG},
};

static void start_advertising_if_ready() {
    if (ble_deinitializing || adv_config_done != 0 || adv_started || ble_connected) return;
    esp_err_t err = esp_ble_gap_start_advertising(&adv_params);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ble_gap_start_advertising failed: %s", esp_err_to_name(err));
    }
}

static void stop_advertising_if_started() {
    if (!adv_started) return;
    esp_err_t err = esp_ble_gap_stop_advertising();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ble_gap_stop_advertising failed: %s", esp_err_to_name(err));
    }
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            ESP_LOGI(TAG, "Advertising data set complete");
            emit_debug("BLE ADV data ready");
            adv_config_done &= ~ADV_CFG_FLAG_ADV_DATA;
            start_advertising_if_ready();
            break;
        case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
            ESP_LOGI(TAG, "Scan response data set complete");
            emit_debug("BLE ADV scan_rsp ready");
            adv_config_done &= ~ADV_CFG_FLAG_SCAN_RSP;
            start_advertising_if_ready();
            break;
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "Advertising started successfully");
                emit_debug("BLE ADV started");
                adv_started = true;
            } else {
                ESP_LOGE(TAG, "Advertising start failed: %d", param->adv_start_cmpl.status);
                emit_debug("BLE ADV start fail=%d", param->adv_start_cmpl.status);
            }
            break;
        case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
            ESP_LOGI(TAG, "Conn params: status=%d, min=%d max=%d, latency=%d timeout=%d",
                     param->update_conn_params.status,
                     param->update_conn_params.min_int,
                     param->update_conn_params.max_int,
                     param->update_conn_params.latency,
                     param->update_conn_params.timeout);
            emit_debug("BLE CONN_PARAM st=%d min=%d max=%d",
                       param->update_conn_params.status,
                       param->update_conn_params.min_int,
                       param->update_conn_params.max_int);
            break;
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            adv_started = false;
            break;
        default:
            break;
    }
}

// Process received data and extract lines
static void process_rx_data(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (data[i] == '\n' || data[i] == '\r') {
            if (rx_buffer_pos > 0) {
                rx_buffer[rx_buffer_pos] = '\0';
                handleLine(rx_buffer);
                rx_buffer_pos = 0;
            }
        } else if (rx_buffer_pos < RX_BUFFER_SIZE - 1) {
            rx_buffer[rx_buffer_pos++] = data[i];
        }
    }
}

// GATTS event handler
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t* param) {
    switch (event) {
        case ESP_GATTS_REG_EVT: {
            ESP_LOGI(TAG, "GATT server registered, app_id: %d", param->reg.app_id);
            ble_gatts_if = gatts_if;
            
            // Set device name
            esp_ble_gap_set_device_name(g_device_name ? g_device_name : "SongLed");
            
            // Keep advertising payload short enough (<31 bytes).
            esp_ble_adv_data_t adv_data = {};
            adv_data.set_scan_rsp = false;
            adv_data.include_name = true;
            adv_data.include_txpower = false;
            adv_data.min_interval = 0x0006;
            adv_data.max_interval = 0x0010;
            adv_data.appearance = 0x00;
            adv_data.manufacturer_len = 0;
            adv_data.p_manufacturer_data = nullptr;
            adv_data.service_data_len = 0;
            adv_data.p_service_data = nullptr;
            adv_data.service_uuid_len = 0;
            adv_data.p_service_uuid = nullptr;
            adv_data.flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);

            esp_ble_adv_data_t scan_rsp_data = {};
            scan_rsp_data.set_scan_rsp = true;
            scan_rsp_data.include_name = false;
            scan_rsp_data.include_txpower = false;
            scan_rsp_data.service_data_len = 0;
            scan_rsp_data.p_service_data = nullptr;
            scan_rsp_data.manufacturer_len = 0;
            scan_rsp_data.p_manufacturer_data = nullptr;
            static uint8_t service_uuid128[16];
            uuid_str_to_128bit(SONGLED_SERVICE_UUID, service_uuid128);
            scan_rsp_data.service_uuid_len = sizeof(service_uuid128);
            scan_rsp_data.p_service_uuid = service_uuid128;

            adv_config_done = ADV_CFG_FLAG_ADV_DATA | ADV_CFG_FLAG_SCAN_RSP;
            esp_err_t adv_err = esp_ble_gap_config_adv_data(&adv_data);
            if (adv_err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ble_gap_config_adv_data failed: %s", esp_err_to_name(adv_err));
                adv_config_done &= ~ADV_CFG_FLAG_ADV_DATA;
            }
            esp_err_t scan_err = esp_ble_gap_config_adv_data(&scan_rsp_data);
            if (scan_err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ble_gap_config_scan_rsp_data failed: %s", esp_err_to_name(scan_err));
                adv_config_done &= ~ADV_CFG_FLAG_SCAN_RSP;
            }
            
            // Create service with 128-bit UUID
            esp_gatt_srvc_id_t service_id = {};
            service_id.is_primary = true;
            service_id.id.inst_id = 0;
            service_id.id.uuid.len = ESP_UUID_LEN_128;
            uuid_str_to_128bit(SONGLED_SERVICE_UUID, service_id.id.uuid.uuid.uuid128);
            
            esp_ble_gatts_create_service(gatts_if, &service_id, 20);
            break;
        }
            
        case ESP_GATTS_CREATE_EVT: {
            ESP_LOGI(TAG, "Service created, handle: %d", param->create.service_handle);
            service_handle = param->create.service_handle;
            esp_ble_gatts_start_service(service_handle);
            
            // Add CMD_TX characteristic (ESP32 -> PC, Notify)
            esp_bt_uuid_t char_uuid_tx = {};
            char_uuid_tx.len = ESP_UUID_LEN_128;
            uuid_str_to_128bit(CHAR_CMD_TX_UUID, char_uuid_tx.uuid.uuid128);
            
            esp_ble_gatts_add_char(service_handle, &char_uuid_tx,
                ESP_GATT_PERM_READ,
                ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
                nullptr, nullptr);
            break;
        }
        
        case ESP_GATTS_ADD_CHAR_EVT: {
            ESP_LOGI(TAG, "Char added, handle: %d, uuid: 0x%02x%02x", 
                param->add_char.attr_handle,
                param->add_char.char_uuid.uuid.uuid128[0],
                param->add_char.char_uuid.uuid.uuid128[1]);
            
            // Store handles (简化版：按添加顺序)
            if (g_char_count == 0) {
                char_cmd_tx_handle = param->add_char.attr_handle;
                // Add CCCD for CMD_TX notifications
                esp_ble_gatts_add_char_descr(service_handle, &cccd_uuid,
                    ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                    nullptr, nullptr);
            } else if (g_char_count == 1) {
                char_cmd_rx_handle = param->add_char.attr_handle;
                
                // Add COVER characteristic
                esp_bt_uuid_t char_uuid_cover = {};
                char_uuid_cover.len = ESP_UUID_LEN_128;
                uuid_str_to_128bit(CHAR_COVER_UUID, char_uuid_cover.uuid.uuid128);
                esp_ble_gatts_add_char(service_handle, &char_uuid_cover,
                    ESP_GATT_PERM_WRITE,
                    ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR,
                    nullptr, nullptr);
            } else if (g_char_count == 2) {
                char_cover_handle = param->add_char.attr_handle;
                ESP_LOGI(TAG, "All characteristics added successfully");
            }
            g_char_count++;
            break;
        }

        case ESP_GATTS_ADD_CHAR_DESCR_EVT: {
            ESP_LOGI(TAG, "Descr added, handle: %d", param->add_char_descr.attr_handle);
            if (char_cmd_tx_cccd_handle == 0) {
                char_cmd_tx_cccd_handle = param->add_char_descr.attr_handle;

                // Add CMD_RX characteristic after CCCD
                esp_bt_uuid_t char_uuid_rx = {};
                char_uuid_rx.len = ESP_UUID_LEN_128;
                uuid_str_to_128bit(CHAR_CMD_RX_UUID, char_uuid_rx.uuid.uuid128);
                esp_ble_gatts_add_char(service_handle, &char_uuid_rx,
                    ESP_GATT_PERM_WRITE,
                    ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR,
                    nullptr, nullptr);
            }
            break;
        }
            
        case ESP_GATTS_START_EVT:
            ESP_LOGI(TAG, "Service started");
            start_advertising_if_ready();
            break;
            
        case ESP_GATTS_CONNECT_EVT: {
            ESP_LOGI(TAG, "Client connected, conn_id: %d", param->connect.conn_id);
            emit_debug("BLE CONNECT conn_id=%d", param->connect.conn_id);
            ble_connected = true;
            ble_conn_id = param->connect.conn_id;
            memcpy(remote_bda, param->connect.remote_bda, sizeof(remote_bda));
            remote_bda_valid = true;
            cmd_tx_notify_enabled = false;
            cmd_rx_seen = false;

            // Request MTU increase for faster transfer
            esp_ble_gatt_set_local_mtu(517);

            if (connection_callback) {
                connection_callback(true);
            }
            break;
        }
            
        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGI(TAG, "Client disconnected, reason=0x%02x", param->disconnect.reason);
            emit_debug("BLE DISC reason=0x%02x", param->disconnect.reason);
            ble_connected = false;
            rx_buffer_pos = 0;
            adv_started = false;
            cmd_tx_notify_enabled = false;
            cmd_rx_seen = false;
            remote_bda_valid = false;
            
            if (connection_callback) {
                connection_callback(false);
            }
            
            // Restart advertising (skip if we are shutting BLE down)
            if (!ble_deinitializing) {
                start_advertising_if_ready();
            }
            break;
            
        case ESP_GATTS_WRITE_EVT:
            if (param->write.handle == char_cmd_tx_cccd_handle && param->write.len == 2) {
                uint16_t cccd = param->write.value[1] << 8 | param->write.value[0];
                ESP_LOGI(TAG, "CCCD updated: 0x%04x", cccd);
                cmd_tx_notify_enabled = (cccd & 0x0001) != 0;
                emit_debug("BLE CCCD notify=%d", cmd_tx_notify_enabled ? 1 : 0);
                if (cmd_tx_notify_enabled) {
                    flush_tx_queue();
                }
            } else if (param->write.handle == char_cmd_rx_handle) {
                // Received command response from PC
                cmd_rx_seen = true;
                emit_debug("BLE RX cmd len=%d", static_cast<int>(param->write.len));
                process_rx_data(param->write.value, param->write.len);
                if (cmd_tx_notify_enabled) {
                    flush_tx_queue();
                }
            } else if (param->write.handle == char_cover_handle) {
                // Received cover data from PC
                emit_debug("BLE RX cover len=%d", static_cast<int>(param->write.len));
                process_rx_data(param->write.value, param->write.len);
            }
            
            // Send write response if needed
            if (param->write.need_rsp) {
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id, 
                                           param->write.trans_id, ESP_GATT_OK, nullptr);
            }
            break;

        case ESP_GATTS_READ_EVT: {
            esp_gatt_rsp_t rsp = {};
            rsp.attr_value.handle = param->read.handle;
            rsp.attr_value.len = 0;
            esp_ble_gatts_send_response(gatts_if, param->read.conn_id,
                                       param->read.trans_id, ESP_GATT_OK, &rsp);
            break;
        }
            
        case ESP_GATTS_MTU_EVT:
            ESP_LOGI(TAG, "MTU set to %d", param->mtu.mtu);
            break;
            
        default:
            break;
    }
}

bool ble_service_init(const char* device_name) {
    ESP_LOGI(TAG, "Initializing BLE service: %s", device_name);
    emit_debug("BLE INIT begin");
    if (ble_initialized) {
        ESP_LOGW(TAG, "BLE already initialized, resetting first");
        ble_service_deinit();
    }
    ble_deinitializing = false;
    reset_runtime_state();

    if (device_name && device_name[0]) {
        g_device_name = device_name;
    }
    
    // Release classic BT memory (we only need BLE)
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    
    // Initialize NVS (required for BLE)
    // (Assuming already done in main)
    
    // Initialize BT controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "BT controller init failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG, "BT controller enable failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Initialize Bluedroid
    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ret = esp_bluedroid_init_with_cfg(&bluedroid_cfg);
    if (ret) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Register callbacks
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GAP callback register failed: %s", esp_err_to_name(ret));
        esp_bluedroid_disable();
        esp_bluedroid_deinit();
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        return false;
    }
    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATTS callback register failed: %s", esp_err_to_name(ret));
        esp_bluedroid_disable();
        esp_bluedroid_deinit();
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        return false;
    }
    
    // Register GATT server app
    ret = esp_ble_gatts_app_register(0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATTS app register failed: %s", esp_err_to_name(ret));
        esp_bluedroid_disable();
        esp_bluedroid_deinit();
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        return false;
    }
    
    ble_initialized = true;
    ESP_LOGI(TAG, "BLE service initialization complete");
    emit_debug("BLE INIT ok");
    return true;
}

void ble_service_deinit() {
    if (!ble_initialized) return;
    emit_debug("BLE DEINIT begin");
    ble_deinitializing = true;
    if (ble_connected) {
        esp_ble_gatts_close(ble_gatts_if, ble_conn_id);
        for (int i = 0; i < 40 && ble_connected; ++i) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    stop_advertising_if_started();
    if (ble_gatts_if != 0) {
        esp_ble_gatts_app_unregister(ble_gatts_if);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED) {
        esp_bluedroid_disable();
    }
    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_INITIALIZED) {
        esp_bluedroid_deinit();
    }
    esp_bt_controller_status_t ctl = esp_bt_controller_get_status();
    if (ctl == ESP_BT_CONTROLLER_STATUS_ENABLED) {
        esp_bt_controller_disable();
        ctl = esp_bt_controller_get_status();
    }
    if (ctl == ESP_BT_CONTROLLER_STATUS_INITED) {
        esp_bt_controller_deinit();
    }
    ble_initialized = false;
    reset_runtime_state();
    ble_deinitializing = false;
    ESP_LOGI(TAG, "BLE service deinitialized");
    emit_debug("BLE DEINIT ok");
}

void ble_disconnect() {
    if (!ble_connected) return;
    esp_ble_gatts_close(ble_gatts_if, ble_conn_id);
}

void ble_restart_advertising() {
    adv_started = false;
    start_advertising_if_ready();
}

bool ble_is_connected() {
    return ble_connected;
}

bool ble_is_notify_ready() {
    return ble_connected && cmd_tx_notify_enabled;
}

bool ble_send_line(const char* line) {
    if (!ble_connected || !line || char_cmd_tx_handle == 0) {
        return false;
    }
    if (!cmd_tx_notify_enabled) {
        return tx_queue_push(line);
    }
    bool ok = ble_send_line_now(line);
    if (!ok) {
        // If link is alive but notify path is momentarily unavailable, keep data.
        tx_queue_push(line);
    }
    return true;
}

void ble_set_connection_callback(ble_connection_callback_t callback) {
    connection_callback = callback;
}

void ble_set_debug_callback(ble_debug_callback_t callback) {
    debug_callback = callback;
}

void ble_process() {
    // Process any pending BLE events
    // (Most processing happens in callbacks)
}
