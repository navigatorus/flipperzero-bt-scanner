#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_bt.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <dialogs/dialogs.h>
#include <toolbox/stream/stream.h>
#include <toolbox/stream/file_stream.h>

#define TAG "BtScanner"
#define MAX_DEVICES 100
#define SCAN_DURATION_MS 45000
#define CHANNEL_COUNT 40
#define RSSI_THRESHOLD -85.0f

typedef enum {
    DeviceTypeUnknown,
    DeviceTypeClassic,
    DeviceTypeBLE,
    DeviceTypeBRE
} BTDeviceType;

typedef struct {
    uint8_t mac[6];
    int8_t rssi;
    char name[32];
    uint32_t first_seen;
    uint32_t last_seen;
    uint16_t packet_count;
    uint8_t channels[CHANNEL_COUNT]; // Активность по каналам
    BTDeviceType type;
    uint8_t signal_strength; // 0-100%
} BTDevice;

typedef struct {
    ViewPort* view_port;
    Gui* gui;
    NotificationApp* notification;
    Storage* storage;
    DialogsApp* dialogs;
} AppContext;

typedef struct {
    AppContext ctx;
    FuriMutex* mutex;
    
    BTDevice devices[MAX_DEVICES];
    uint16_t device_count;
    bool scanning;
    uint32_t scan_start_time;
    bool new_device_found;
    
    uint16_t selected_index;
    bool show_details;
    bool show_spectrum;
    
    // Статистика сканирования
    uint32_t total_packets;
    uint8_t channel_activity[CHANNEL_COUNT];
    float max_rssi;
    float min_rssi;
} BtScannerState;

// Прототипы функций
void bt_scanner_start_scan(BtScannerState* state);
void bt_scanner_stop_scan(BtScannerState* state);
bool bt_scanner_save_log(BtScannerState* state);
void bt_scanner_draw_callback(Canvas* canvas, void* context);
void bt_scanner_input_callback(InputEvent* input_event, void* context);
BtScannerState* bt_scanner_alloc();
void bt_scanner_free(BtScannerState* state);
