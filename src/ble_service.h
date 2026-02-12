#pragma once

#include <stdint.h>
#include <stdbool.h>

// BLE Service UUID: SongLed Control Service
#define SONGLED_SERVICE_UUID        "12345678-1234-5678-1234-56789abcdef0"

// Characteristic UUIDs
#define CHAR_CMD_TX_UUID            "12345678-1234-5678-1234-56789abcdef1"  // ESP32 -> PC (commands)
#define CHAR_CMD_RX_UUID            "12345678-1234-5678-1234-56789abcdef2"  // PC -> ESP32 (responses)
#define CHAR_COVER_UUID             "12345678-1234-5678-1234-56789abcdef3"  // PC -> ESP32 (cover data)
#define CHAR_STATUS_UUID            "12345678-1234-5678-1234-56789abcdef4"  // ESP32 -> PC (notify status)

// BLE connection state callback
typedef void (*ble_connection_callback_t)(bool connected);

// Initialize BLE service
bool ble_service_init(const char* device_name);

// Deinitialize BLE service
void ble_service_deinit();

// Disconnect current BLE client (if any)
void ble_disconnect();

// Restart BLE advertising
void ble_restart_advertising();

// Check if BLE is connected
bool ble_is_connected();

// Send a line of text to PC (replaces sendLine for BLE)
bool ble_send_line(const char* line);

// Register callback for connection state changes
void ble_set_connection_callback(ble_connection_callback_t callback);

// Process incoming BLE data (call from main loop)
void ble_process();
