#include "bt_scanner.h"

static void analyze_patterns(BtTestApp* app) {
    int ble_adv_channels = 0;    // BLE advertising каналы (37,38,39)
    int ble_data_channels = 0;   // BLE data каналы (0-36)
    int classic_bt_channels = 0; // Классический Bluetooth
    float total_rssi = 0;
    int active_channels = 0;
    
    // Анализируем активность по каналам
    for(int i = 0; i < MAX_CHANNELS; i++) {
        if(app->channel_activity[i] > 0) {
            active_channels++;
            total_rssi += app->channel_rssi[i];
            
            if(i >= 37 && i <= 39) {
                ble_adv_channels++;
            } else if(i >= 0 && i <= 36) {
                ble_data_channels++;
            }
        }
    }
    
    // Оценка количества устройств
    if(active_channels == 0) {
        app->estimated_devices = 0;
        strcpy(app->pattern_info, "No activity");
    } else {
        // Простая эвристика для оценки количества устройств
        if(ble_adv_channels >= 2) {
            // BLE устройства обычно рекламируются на 2-3 каналах
            app->estimated_devices = ble_adv_channels;
            strcpy(app->pattern_info, "BLE devices");
        } else if(active_channels > 10) {
            // Много активности - возможно классический Bluetooth
            app->estimated_devices = active_channels / 3;
            strcpy(app->pattern_info, "Classic BT");
        } else {
            // Смешанная активность
            app->estimated_devices = (ble_adv_channels + ble_data_channels) / 2;
            strcpy(app->pattern_info, "Mixed signals");
        }
    }
    
    FURI_LOG_I(TAG, "Pattern analysis: BLE_adv=%d, BLE_data=%d, Active=%d, Est_devices=%d", 
               ble_adv_channels, ble_data_channels, active_channels, app->estimated_devices);
}

static void bt_real_scan(BtTestApp* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->scanning = true;
    app->device_found = false;
    strcpy(app->status, "Analyzing...");
    furi_mutex_release(app->mutex);
    
    view_port_update(app->view_port);
    furi_delay_ms(500);
    
    bool found_activity = false;
    
    // Сканируем все 40 BLE каналов для анализа паттернов
    for(int channel = 0; channel < MAX_CHANNELS; channel++) {
        if(!view_port_is_enabled(app->view_port)) break;
        
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        snprintf(app->status, sizeof(app->status), "Ch %d/%d", channel + 1, MAX_CHANNELS);
        furi_mutex_release(app->mutex);
        
        view_port_update(app->view_port);
        
        // Сканируем канал
        furi_hal_bt_start_packet_rx(channel, 1);
        furi_delay_ms(30); // Быстрое сканирование для паттернов
        
        float rssi = furi_hal_bt_get_rssi();
        furi_hal_bt_stop_packet_test();
        
        // Обновляем статистику канала
        if(rssi > -95.0f) {
            found_activity = true;
            app->channel_activity[channel]++;
            app->channel_rssi[channel] = (app->channel_rssi[channel] + rssi) / 2.0f; // Скользящее среднее
        }
        
        furi_delay_ms(20);
    }
    
    // Анализируем паттерны
    analyze_patterns(app);
    
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->scanning = false;
    app->scan_count++;
    
    if(found_activity) {
        if(app->estimated_devices > 0) {
            snprintf(app->status, sizeof(app->status), "%d devices est.", app->estimated_devices);
        } else {
            strcpy(app->status, "Signals found");
        }
        app->device_found = true;
    } else {
        strcpy(app->status, "No signals");
        app->device_found = false;
        app->estimated_devices = 0;
    }
    
    furi_mutex_release(app->mutex);
}

static void bt_test_app_draw_callback(Canvas* canvas, void* context) {
    BtTestApp* app = context;
    
    if(furi_mutex_acquire(app->mutex, 100) != FuriStatusOk) return;
    
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    
    canvas_draw_str(canvas, 2, 10, "BT Signal Analyzer");
    canvas_draw_line(canvas, 0, 12, 127, 12);
    
    canvas_set_font(canvas, FontSecondary);
    
    // Статус и оценка устройств
    canvas_draw_str(canvas, 2, 24, app->status);
    
    if(app->scanning) {
        canvas_draw_str(canvas, 2, 36, "Scanning 40 channels...");
        canvas_draw_str(canvas, 2, 46, "Pattern analysis");
    } else if(app->device_found) {
        // Показываем анализ
        if(app->estimated_devices > 0) {
            char devices_str[32];
            snprintf(devices_str, sizeof(devices_str), "Est: %d devices", app->estimated_devices);
            canvas_draw_str(canvas, 2, 36, devices_str);
        } else {
            canvas_draw_str(canvas, 2, 36, "Signals detected");
        }
        canvas_draw_str(canvas, 2, 46, app->pattern_info);
    } else {
        if(strcmp(app->status, "Press OK to scan") == 0) {
            canvas_draw_str(canvas, 2, 36, "Press OK to start");
            canvas_draw_str(canvas, 2, 46, "signal analysis");
        } else {
            canvas_draw_str(canvas, 2, 36, "Analysis complete");
            canvas_draw_str(canvas, 2, 46, "No signals found");
        }
    }
    
    // Информация о сканированиях
    char scan_info[32];
    snprintf(scan_info, sizeof(scan_info), "Scans: %d", app->scan_count);
    canvas_draw_str(canvas, 90, 24, scan_info);
    
    // Простая визуализация активности каналов
    if(app->scan_count > 0) {
        canvas_draw_str(canvas, 2, 58, "Ch37-39:");
        
        // Индикаторы BLE advertising каналов
        for(int i = 37; i <= 39; i++) {
            int bars = (app->channel_activity[i] > 0) ? 3 : 1;
            int x_pos = 50 + (i - 37) * 15;
            
            for(int b = 0; b < bars; b++) {
                canvas_draw_box(canvas, x_pos, 58 - (b * 2), 2, 2 + (b * 2));
            }
        }
    }
    
    canvas_draw_line(canvas, 0, 52, 127, 52);
    canvas_draw_str(canvas, 2, 62, "OK=Scan");
    canvas_draw_str(canvas, 60, 62, "Back=Exit");
    
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
    
    // Инициализация анализа
    app->scanning = false;
    app->device_found = false;
    app->scan_count = 0;
    app->estimated_devices = 0;
    strcpy(app->status, "Press OK to scan");
    strcpy(app->pattern_info, "Ready");
    
    for(int i = 0; i < MAX_CHANNELS; i++) {
        app->channel_activity[i] = 0;
        app->channel_rssi[i] = -100.0f;
    }
    
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
    
    FURI_LOG_I(TAG, "BT Signal Analyzer started");
    
    while(view_port_is_enabled(app->view_port)) {
        view_port_update(app->view_port);
        furi_delay_ms(50);
    }
    
    bt_test_app_free(app);
    FURI_LOG_I(TAG, "BT Signal Analyzer stopped");
    return 0;
}
