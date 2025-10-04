#include "bt_scanner.h"

#define TAG "BtScanner"

static void bt_real_scan(BtTestApp* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->scanning = true;
    app->device_found = false;
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

void bt_test_app_draw_callback(Canvas* canvas, void* context) {
    BtTestApp* app = context;
    
    if(furi_mutex_acquire(app->mutex, 100) != FuriStatusOk) return;
    
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "BT Device Scanner");
    canvas_draw_line(canvas, 0, 12, 127, 12);
    
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 25, app->status);
    
    if(app->scanning) {
        canvas_draw_str(canvas, 2, 40, "Scanning...");
        canvas_draw_str(canvas, 2, 50, "Check nearby devices");
    } else if(app->device_found) {
        canvas_draw_str(canvas, 2, 40, "BT devices detected!");
        canvas_draw_str(canvas, 2, 50, "Try full scanner app");
    } else {
        canvas_draw_str(canvas, 2, 40, "No devices found");
        canvas_draw_str(canvas, 2, 50, "Ensure BT is enabled");
    }
    
    canvas_draw_line(canvas, 0, 55, 127, 55);
    canvas_draw_str(canvas, 2, 65, "OK=Scan  Back=Exit");
    
    furi_mutex_release(app->mutex);
}

void bt_test_app_input_callback(InputEvent* input_event, void* context) {
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
    
    while(view_port_is_enabled(app->view_port)) {
        view_port_update(app->view_port);
        furi_delay_ms(50);
    }
    
    bt_test_app_free(app);
    return 0;
}
