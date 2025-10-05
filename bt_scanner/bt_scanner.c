#include "bt_scanner.h"

static void bt_real_scan(BtTestApp* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->scanning = true;
    app->device_found = false;
    strcpy(app->status, "Scanning...");
    furi_mutex_release(app->mutex);
    
    view_port_update(app->view_port);
    furi_delay_ms(100);
    
    bool found_activity = false;
    int active_channels = 0;
    float best_rssi = -100.0f; // Для отслеживания лучшего сигнала
    
    int ble_channels[] = {37, 38, 39};
    
    for(int i = 0; i < 3; i++) {
        if(!view_port_is_enabled(app->view_port)) break;
        
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        snprintf(app->status, sizeof(app->status), "Channel %d", ble_channels[i]);
        furi_mutex_release(app->mutex);
        
        view_port_update(app->view_port);
        
        furi_hal_bt_start_packet_rx(ble_channels[i], 1);
        furi_delay_ms(100);
        float rssi = furi_hal_bt_get_rssi();
        furi_hal_bt_stop_packet_test();
        
        // Сохраняем лучший RSSI
        if(rssi > best_rssi) {
            best_rssi = rssi;
        }
        
        if(rssi > -90.0f) {
            found_activity = true;
            active_channels++;
        }
        
        furi_delay_ms(50);
    }
    
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->scanning = false;
    
    if(found_activity) {
        // Показываем количество каналов и лучший RSSI
        snprintf(app->status, sizeof(app->status), "Found %d ch, %.1f dB", active_channels, (double)best_rssi);
        app->device_found = true;
    } else {
        snprintf(app->status, sizeof(app->status), "No devices (%.1f dB)", (double)best_rssi);
        app->device_found = false;
    }
    
    furi_mutex_release(app->mutex);
}

static void bt_test_app_draw_callback(Canvas* canvas, void* context) {
    BtTestApp* app = context;
    
    if(furi_mutex_acquire(app->mutex, 100) != FuriStatusOk) return;
    
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    
    // Заголовок
    canvas_draw_str(canvas, 2, 10, "BT Scanner");
    canvas_draw_line(canvas, 0, 12, 127, 12);
    
    canvas_set_font(canvas, FontSecondary);
    
    // Статус сканирования (теперь с RSSI)
    canvas_draw_str(canvas, 2, 24, app->status);
    
    // Детали в зависимости от состояния
    if(app->scanning) {
        canvas_draw_str(canvas, 2, 36, "Scanning BLE...");
        canvas_draw_str(canvas, 2, 46, "Ch 37,38,39");
    } else if(app->device_found) {
        canvas_draw_str(canvas, 2, 36, "Devices found!");
        canvas_draw_str(canvas, 2, 46, "BT active nearby");
    } else {
        if(strcmp(app->status, "Press OK to scan") == 0) {
            canvas_draw_str(canvas, 2, 36, "Press OK button");
            canvas_draw_str(canvas, 2, 46, "to start scan");
        } else {
            canvas_draw_str(canvas, 2, 36, "Scan complete");
            canvas_draw_str(canvas, 2, 46, "No BT activity");
        }
    }
    
    // Подсказки
    canvas_draw_line(canvas, 0, 52, 127, 52);
    canvas_draw_str(canvas, 2, 60, "OK=Scan");
    canvas_draw_str(canvas, 60, 60, "Back=Exit");
    
    furi_mutex_release(app->mutex);
}

// Остальной код без изменений...
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
    
    FURI_LOG_I(TAG, "BT Scanner started");
    
    while(view_port_is_enabled(app->view_port)) {
        view_port_update(app->view_port);
        furi_delay_ms(50);
    }
    
    bt_test_app_free(app);
    FURI_LOG_I(TAG, "BT Scanner stopped");
    return 0;
}
