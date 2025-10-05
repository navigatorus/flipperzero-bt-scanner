#include "bt_scanner.h"

static void bt_real_scan(BtTestApp* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->scanning = true;
    app->device_found = false;
    strcpy(app->status, "Starting scan...");
    furi_mutex_release(app->mutex);
    
    view_port_update(app->view_port);
    furi_delay_ms(500);
    
    bool found_activity = false;
    
    // ОСТОРОЖНО: Отключаем BLE перед сканированием
    furi_hal_bt_stop_advertising();
    furi_delay_ms(100);
    
    int ble_channels[] = {37, 38, 39};
    
    for(int i = 0; i < 3; i++) {
        // Проверяем не вышли ли из приложения
        if(!view_port_is_enabled(app->view_port)) break;
        
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        snprintf(app->status, sizeof(app->status), "Scan ch %d...", ble_channels[i]);
        furi_mutex_release(app->mutex);
        
        view_port_update(app->view_port);
        
        // Безопасное сканирование с проверкой ошибок
        furi_hal_bt_start_packet_rx(ble_channels[i], 1);
        furi_delay_ms(30); // Уменьшили время
        
        float rssi = -100.0f; // Значение по умолчанию
        if(furi_hal_bt_is_active()) {
            rssi = furi_hal_bt_get_rssi();
        }
        
        furi_hal_bt_stop_packet_test();
        furi_delay_ms(50); // Пауза между каналами
        
        FURI_LOG_D(TAG, "Channel %d RSSI: %.1f", ble_channels[i], (double)rssi);
        
        if(rssi > -85.0f) {
            found_activity = true;
            FURI_LOG_I(TAG, "Activity found on channel %d", ble_channels[i]);
        }
    }
    
    // Восстанавливаем BLE
    furi_hal_bt_start_advertising();
    
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->scanning = false;
    
    if(found_activity) {
        strcpy(app->status, "BT devices found!");
        app->device_found = true;
    } else {
        strcpy(app->status, "No devices found");
        app->device_found = false;
    }
    
    furi_mutex_release(app->mutex);
}

static void bt_test_app_draw_callback(Canvas* canvas, void* context) {
    BtTestApp* app = context;
    
    if(furi_mutex_acquire(app->mutex, 100) != FuriStatusOk) return;
    
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    
    // Заголовок - подняли выше
    canvas_draw_str(canvas, 2, 8, "BT Device Scanner");
    canvas_draw_line(canvas, 0, 10, 127, 10);
    
    canvas_set_font(canvas, FontSecondary);
    
    // Статус
    canvas_draw_str(canvas, 2, 22, app->status);
    
    // Основной контент
    if(app->scanning) {
        canvas_draw_str(canvas, 2, 34, "Scanning...");
        canvas_draw_str(canvas, 2, 44, "Channels: 37,38,39");
    } else if(app->device_found) {
        canvas_draw_str(canvas, 2, 34, "Devices nearby!");
        canvas_draw_str(canvas, 2, 44, "BLE activity detected");
    } else {
        if(strcmp(app->status, "Press OK to scan") == 0) {
            canvas_draw_str(canvas, 2, 34, "Press OK to start");
            canvas_draw_str(canvas, 2, 44, "BT device scan");
        } else {
            canvas_draw_str(canvas, 2, 34, "Scan complete");
            canvas_draw_str(canvas, 2, 44, "No devices found");
        }
    }
    
    // Подсказки управления - подняли ВЫШЕ и сделали компактнее
    canvas_draw_line(canvas, 0, 50, 127, 50); // Подняли линию
    canvas_draw_str(canvas, 2, 58, "OK=Scan"); // Подняли текст
    canvas_draw_str(canvas, 60, 58, "Back=Exit");
    
    furi_mutex_release(app->mutex);
}

static void bt_test_app_input_callback(InputEvent* input_event, void* context) {
    BtTestApp* app = context;
    
    if(input_event->type == InputTypeShort) {
        if(input_event->key == InputKeyOk) {
            if(!app->scanning) {
                // Запускаем в отдельном потоке чтобы не блокировать UI
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
    
    // Восстанавливаем BLE при выходе
    furi_hal_bt_start_advertising();
    
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
