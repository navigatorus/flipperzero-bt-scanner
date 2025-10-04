#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification.h>

#define TAG "BtScanner"

typedef struct {
    ViewPort* view_port;
    Gui* gui;
    NotificationApp* notification;
    FuriMutex* mutex;
    
    bool scanning;
    bool device_found;
    char status[64];
} BtTestApp;

BtTestApp* bt_test_app_alloc();
void bt_test_app_free(BtTestApp* app);
int32_t bt_scanner_app(void* p);

#ifdef __cplusplus
}
#endif
