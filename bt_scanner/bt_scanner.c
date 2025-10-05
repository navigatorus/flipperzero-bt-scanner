#include "bt_scanner.h"

static void bt_real_scan(BtTestApp* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->scanning = true;
    app->device_found = false;
    app->scroll_offset = 0;
    strcpy(app->status, "Scanning BT devices...");
    furi_mutex_release(app->mutex);
    
    bool found_activity = false;
    int active_channels = 0;
    
    int ble_channels[] = {37, 38, 39};
    
    for(int i = 0; i < 3; i++) {
        // Проверяем не вышли ли из приложения
        if(!view_port_is_enabled(app->view_port)) break;
        
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        snprintf(app->status, sizeof(app->status), "Scanning BLE ch %d", ble_channels[i]);
        furi_mutex_release(app->mutex);
        
        view_port_update(app->view_port);
        
        furi_hal_bt_start_packet_rx(ble_channels[i], 1);
        furi_delay_ms(50);
        float rssi = furi_hal_bt_get_rssi();
        furi_hal_bt_stop_packet_test();
        
        if(rssi > -85.0f) {
            found_activity = true;
            active_channels++;
        }
        
        furi_delay_ms(100);
    }
    
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->scanning = false;
    
    if(found_activity) {
        snprintf(app->status, sizeof(app->status), "Found activity! %d ch", active_channels);
        app->device_found = true;
    } else {
        strcpy(app->status, "No BT devices found");
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
    canvas_draw_str(canvas, 2, 10, "BT Device Scanner");
    canvas_draw_line(canvas, 0, 12, 127, 12);
    
    canvas_set_font(canvas, FontSecondary);
    
    // Статус
    canvas_draw_str(canvas, 2, 24, app->status);
    
    // Основной контент в зависимости от состояния
    if(app->scanning) {
        canvas_draw_str(canvas, 2, 36, "Scanning BLE channels...");
        canvas_draw_str(canvas, 2, 46, "37, 38, 39");
    } else if(app->device_found) {
        // Только после сканирования показываем детали
        canvas_draw_str(canvas, 2, 36, "Devices detected nearby!");
        canvas_draw_str(canvas, 2, 46, "BLE activity found");
    } else {
        // До сканирования показываем только инструкцию
        if(strcmp(app->status, "Press OK to scan") == 0) {
            canvas_draw_str(canvas, 2, 36, "Press OK to start scan");
            canvas_draw_str(canvas, 2, 46, "for BT devices");
        } else {
            // После сканирования, если ничего не найдено
            canvas_draw_str(canvas, 2, 36, "No devices found");
            canvas_draw_str(canvas, 2, 46, "Try again later");
        }
    }
    
    // Подсказки управления - поднимаем выше и делаем короче
    canvas_draw_line(canvas, 0, 53, 127, 53);
    canvas_draw_str(canvas, 2, 60, "OK=Scan");
    canvas_draw_str(canvas, 50, 60, "Back=Exit");
    
    furi_mutex_release(app->mutex);
}

static void bt_test_app_input_callback(InputEvent* input_event, void* context) {
    BtTestApp* app = context;
    
    if(furi_mutex_acquire(app->mutex, 100) != FuriStatusOk) return;

    if(input_event->type == InputTypeShort) {
        switch(input_event->key) {
        case InputKeyOk:
            if(!app->scanning) {
                bt_real_scan(app);
            }
            break;
        case InputKeyBack:
            // Правильный выход из приложения
            view_port_enabled_set(app->view_port, false);
            break;
        default:
            break;
        }
    }

    furi_mutex_release(app->mutex);
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
    app->scroll_offset = 0;
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
    
    // Главный цикл - проверяем включен ли view_port
    while(view_port_is_enabled(app->view_port)) {
        view_port_update(app->view_port);
        furi_delay_ms(50);
    }
    
    bt_test_app_free(app);
    return 0;
}
