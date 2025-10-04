#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification.h>
#include <storage/storage.h>

#define TAG "BtScanner"
#define MAX_DEVICES 50
#define SCAN_DURATION_MS 30000
#define RSSI_THRESHOLD -80.0f

typedef struct {
    uint8_t mac[6];
    int8_t rssi;
    char name[32];
    uint32_t first_seen;
    uint32_t last_seen;
    uint8_t channel;
    uint16_t packet_count;
    uint8_t device_type;
} BTDevice;

typedef struct {
    ViewPort* view_port;
    Gui* gui;
    NotificationApp* notification;
    FuriMutex* mutex;
    
    BTDevice devices[MAX_DEVICES];
    uint16_t device_count;
    bool scanning;
    uint32_t scan_start_time;
    bool new_device_found;
    
    uint16_t selected_index;
    bool show_details;
} BtScannerState;

BtScannerState* bt_scanner_alloc();
void bt_scanner_free(BtScannerState* state);
int32_t bt_scanner_app(void* p);
