#include "bt_scanner.h"

static void bt_test_scan(BtTestApp* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->scanning = true;
    app->device_found = false;
    strcpy(app->status, "Scanning...");
    furi_mutex_release(app->mutex);
    
    notification_message(app->notification, &sequence_blink_start_blue);
    
    // Простая попытка сканирования
    bool found_something = false;
    
    // Попробуем использовать доступные функции
    for(int attempt = 0; attempt < 3; attempt++) {
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        snprintf(app->status, sizeof(app->status), "Scanning... Attempt %d/3", attempt + 1);
        furi_mutex_release(app->mutex);
        
        view_port_update(app->view_port);
        furi_delay_ms(1000);
        
        // Проверяем доступность BLE
        if(furi_hal_bt_is_active()) {
            found_something = true;
            break;
        }
        
        // Короткая пауза между попытками
        furi_delay_ms(500);
    }
    
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->scanning = false;
    
    if(found_something) {
        strcpy(app->status, "BLE Stack: ACTIVE");
        app->device_found = true;
        notification_message(app->notification, &sequence_success);
    } else {
        strcpy(app->status, "No BLE activity detected");
        app->device_found = false;
        notification_message(app->notification, &sequence_error);
    }
    
    furi_mutex_release(app->mutex);
    notification_message(app->notification, &sequence_blink_stop);
}

void bt_test_app_draw_callback(Canvas* canvas, void* context) {
    BtTestApp* app = context;
    
    if(furi_mutex_acquire(app->mutex, 100) != FuriStatusOk) return;
    
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "BT Scanner Test");
    canvas_draw_line(canvas, 0, 12, 127, 12);
    
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 25, app->status);
    
    if(app->device_found) {
        canvas_draw_str(canvas, 2, 40, "Device: BLE Stack");
        canvas_draw_str(canvas, 2, 50, "Status: Active");
    } else {
        canvas_draw_str(canvas, 2, 40, "No devices found");
    }
    
    canvas_draw_line(canvas, 0, 55, 127, 55);
    canvas_draw_str(canvas, 2, 65, "OK: Scan  Back: Exit");
    
    furi_mutex_release(app->mutex);
}

void bt_test_app_input_callback(InputEvent* input_event, void* context) {
    BtTestApp* app = context;
    
    if(input_event->type == InputTypeShort) {
        if(input_event->key == InputKeyOk) {
            if(!app->scanning) {
                bt_test_scan(app);
            }
        } else if(input_event->key == InputKeyBack) {
            // Выход из приложения
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
    
    // Главный цикл
    while(view_port_is_enabled(app->view_port)) {
        view_port_update(app->view_port);
        furi_delay_ms(50);
    }
    
    bt_test_app_free(app);
    return 0;
}
