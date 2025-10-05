#include "bt_scanner.h"

static void bt_real_scan(BtTestApp* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->scanning = true;
    app->device_found = false;
    strcpy(app->status, "Testing BLE...");
    furi_mutex_release(app->mutex);
    
    view_port_update(app->view_port);
    furi_delay_ms(500);
    
    bool found_activity = false;
    
    // Тест 1: Проверяем доступность BLE функций
    bool ble_supported = furi_hal_bt_is_gatt_gap_supported();
    bool ble_active = furi_hal_bt_is_active();
    
    FURI_LOG_I(TAG, "BLE supported: %d, active: %d", ble_supported, ble_active);
    
    // Тест 2: Пробуем разные методы обнаружения
    int test_channels[] = {37, 38, 39, 0, 1, 2};
    
    for(int i = 0; i < 6; i++) {
        if(!view_port_is_enabled(app->view_port)) break;
        
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        snprintf(app->status, sizeof(app->status), "Test %d/6", i + 1);
        furi_mutex_release(app->mutex);
        
        view_port_update(app->view_port);
        
        // Пробуем разные методы
        furi_hal_bt_start_packet_rx(test_channels[i], 1);
        furi_delay_ms(50);
        
        float rssi = furi_hal_bt_get_rssi();
        furi_hal_bt_stop_packet_test();
        
        FURI_LOG_I(TAG, "Test %d (ch %d): RSSI %.1f", i + 1, test_channels[i], (double)rssi);
        
        // Если RSSI не -100, значит что-то есть
        if(rssi > -99.0f) {
            found_activity = true;
            FURI_LOG_W(TAG, "SIGNAL DETECTED! Channel %d: %.1f dB", test_channels[i], (double)rssi);
        }
        
        furi_delay_ms(100);
    }
    
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->scanning = false;
    
    if(found_activity) {
        strcpy(app->status, "BLE signals found!");
        app->device_found = true;
    } else {
        strcpy(app->status, "No BLE signals");
        app->device_found = false;
    }
    
    furi_mutex_release(app->mutex);
}

static void bt_test_app_draw_callback(Canvas* canvas, void* context) {
    BtTestApp* app = context;
    
    if(furi_mutex_acquire(app->mutex, 100) != FuriStatusOk) return;
    
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    
    canvas_draw_str(canvas, 2, 10, "BT Signal Detector");
    canvas_draw_line(canvas, 0, 12, 127, 12);
    
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 24, app->status);
    
    if(app->scanning) {
        canvas_draw_str(canvas, 2, 36, "Testing RF signals...");
        canvas_draw_str(canvas, 2, 46, "6 channels");
    } else if(app->device_found) {
        canvas_draw_str(canvas, 2, 36, "RF activity found!");
        canvas_draw_str(canvas, 2, 46, "BLE signals detected");
    } else {
        if(strcmp(app->status, "Press OK to scan") == 0) {
            canvas_draw_str(canvas, 2, 36, "Press OK to test");
            canvas_draw_str(canvas, 2, 46, "for BLE signals");
        } else {
            canvas_draw_str(canvas, 2, 36, "Test complete");
            canvas_draw_str(canvas, 2, 46, "No RF signals");
        }
    }
    
    canvas_draw_line(canvas, 0, 52, 127, 52);
    canvas_draw_str(canvas, 2, 60, "OK=Test");
    canvas_draw_str(canvas, 60, 60, "Back=Exit");
    
    furi_mutex_release(app->mutex);
}

static void bt_test_app_input_callback(InputEvent* input_event, void* context) {
    BtTestApp* app = context;
    
    if(input_event->type == InputTypeShort) {
        if(input_event->key == InputKeyOk) {
            if(!app->scanning) {
                bt_real_scan(app);
            }
        } else if(input_event->key == InputKeyBack) {
            view_port_enabled_set(app->view_port, false);
        }
    }
}

BtTestApp* bt_test_app_alloc() {
    BtTestApp* app = malloc(sizeof(BtTestApp));
    
    app->gui = furi_record_open(RECORD_GUI);
    app->notification = furi_record_open(RECORD_NOTIFICATION);
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, bt_test_app_draw_callback, app);
    view_port_input_callback_set(app->view_port, bt_test_app_input_callback, app);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
    
    app->scanning = false;
    app->device_found = false;
    strcpy(app->status, "Press OK to scan");
    
    return app;
}

void bt_test_app_free(BtTestApp* app) {
    if(!app) return;
    
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_mutex_free(app->mutex);
    
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    
    free(app);
}

int32_t bt_scanner_app(void* p) {
    UNUSED(p);
    
    BtTestApp* app = bt_test_app_alloc();
    
    FURI_LOG_I(TAG, "BT Signal Detector started");
    
    while(view_port_is_enabled(app->view_port)) {
        view_port_update(app->view_port);
        furi_delay_ms(50);
    }
    
    bt_test_app_free(app);
    FURI_LOG_I(TAG, "BT Signal Detector stopped");
    return 0;
}
