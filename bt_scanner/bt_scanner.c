#include "bt_scanner.h"

static void bt_real_scan(BtTestApp* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->scanning = true;
    app->device_found = false;
    app->scroll_offset = 0;  // Сбрасываем скролл при новом сканировании
    strcpy(app->status, "Scanning BT devices...");
    furi_mutex_release(app->mutex);
    
    bool found_activity = false;
    int active_channels = 0;
    
    // Сканируем основные BLE каналы
    int ble_channels[] = {37, 38, 39}; // BLE advertising channels
    
    // Сканируем BLE каналы
    for(int i = 0; i < 3; i++) {
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        snprintf(app->status, sizeof(app->status), "Scanning BLE ch %d", ble_channels[i]);
        furi_mutex_release(app->mutex);
        
        view_port_update(app->view_port);
        
        // Пытаемся обнаружить активность на канале
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
    
    // Заголовок (всегда видим)
    canvas_draw_str(canvas, 2, 10 + app->scroll_offset, "BT Device Scanner");
    canvas_draw_line(canvas, 0, 12 + app->scroll_offset, 127, 12 + app->scroll_offset);
    
    canvas_set_font(canvas, FontSecondary);
    
    // Статус сканирования
    canvas_draw_str(canvas, 2, 25 + app->scroll_offset, app->status);
    
    // Основной контент с учетом скролла
    if(app->scanning) {
        canvas_draw_str(canvas, 2, 40 + app->scroll_offset, "Scanning...");
        canvas_draw_str(canvas, 2, 50 + app->scroll_offset, "Check nearby devices");
        canvas_draw_str(canvas, 2, 60 + app->scroll_offset, "BLE channels: 37,38,39");
    } else if(app->device_found) {
        canvas_draw_str(canvas, 2, 40 + app->scroll_offset, "BT devices detected!");
        canvas_draw_str(canvas, 2, 50 + app->scroll_offset, "Try full scanner app");
        canvas_draw_str(canvas, 2, 60 + app->scroll_offset, "with advanced features");
        canvas_draw_str(canvas, 2, 70 + app->scroll_offset, "and device list");
    } else {
        canvas_draw_str(canvas, 2, 40 + app->scroll_offset, "No devices found");
        canvas_draw_str(canvas, 2, 50 + app->scroll_offset, "Ensure BT is enabled");
        canvas_draw_str(canvas, 2, 60 + app->scroll_offset, "on nearby devices");
        canvas_draw_str(canvas, 2, 70 + app->scroll_offset, "and try again");
    }
    
    // Подсказки управления (всегда внизу экрана)
    canvas_draw_line(canvas, 0, 55, 127, 55);
    canvas_draw_str(canvas, 2, 62, "OK=Scan");
    canvas_draw_str(canvas, 45, 62, "Up/Down=Scroll");
    canvas_draw_str(canvas, 2, 70, "Back=Exit");
    
    // Индикатор скролла (если есть что скроллить)
    if(app->scroll_offset < 0) {
        canvas_draw_str(canvas, 120, 62, "^");
    }
    if(app->scroll_offset > -20) { // Максимальный скролл вниз
        canvas_draw_str(canvas, 120, 70, "v");
    }
    
    furi_mutex_release(app->mutex);
}

static void bt_test_app_input_callback(InputEvent* input_event, void* context) {
    BtTestApp* app = context;
    
    if(furi_mutex_acquire(app->mutex, 100) != FuriStatusOk) return;

    if(input_event->type == InputTypeShort) {
        switch(input_event->key) {
        case InputKeyUp:
            // Скролл вверх (увеличиваем offset)
            if(app->scroll_offset < 0) {
                app->scroll_offset += 10;
                if(app->scroll_offset > 0) app->scroll_offset = 0;
            }
            break;
        case InputKeyDown:
            // Скролл вниз (уменьшаем offset)
            if(app->scroll_offset > -30) { // Ограничиваем максимальный скролл
                app->scroll_offset -= 10;
            }
            break;
        case InputKeyOk:
            if(!app->scanning) {
                bt_real_scan(app);
            }
            break;
        case InputKeyBack:
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
    app->scroll_offset = 0;  // Изначально без скролла
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
    
    while(view_port_is_enabled(app->view_port)) {
        view_port_update(app->view_port);
        furi_delay_ms(50);
    }
    
    bt_test_app_free(app);
    return 0;
}
