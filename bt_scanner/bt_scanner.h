#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification.h>

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    NotificationApp* notification;
    FuriMutex* mutex;
    bool scanning;
    bool device_found;
    char status[64];
} BtTestApp;

BtTestApp* bt_test_app_alloc();
void bt_test_app_free(BtTestApp* app);
void bt_test_app_draw_callback(Canvas* canvas, void* context);
void bt_test_app_input_callback(InputEvent* input_event, void* context);
