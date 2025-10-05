#include "bt_scanner.h"

static void analyze_patterns(BtTestApp* app) {
    int ble_adv_channels = 0;    // BLE advertising каналы (37,38,39)
    int ble_data_channels = 0;   // BLE data каналы (0-36) 
    int active_channels = 0;
    int strong_signals = 0;
    
    // Анализируем активность по каналам
    for(int i = 0; i < MAX_CHANNELS; i++) {
        if(app->channel_activity[i] > 0) {
            active_channels++;
            
            // Сигналы сильнее -80 dB считаем сильными
            if(app->channel_rssi[i] > -80.0f) {
                strong_signals++;
            }
            
            if(i >= 37 && i <= 39) {
                ble_adv_channels++;
            } else if(i >= 0 && i <= 36) {
                ble_data_channels++;
            }
        }
    }
    
    // Более точная оценка
    if(active_channels == 0) {
        app->estimated_devices = 0;
        strcpy(app->pattern_info, "No signals");
    } else if(strong_signals >= 2) {
        // Сильные сигналы на нескольких каналах - вероятно классический BT
        app->estimated_devices = strong_signals;
        strcpy(app->pattern_info, "Classic BT");
    } else if(ble_adv_channels >= 2) {
        // BLE advertising на 2-3 каналах
        app->estimated_devices = ble_adv_channels;
        strcpy(app->pattern_info, "BLE devices");
    } else {
        // Слабые или одиночные сигналы
        app->estimated_devices = 0;
        strcpy(app->pattern_info, "Weak signals");
    }
    
    FURI_LOG_I(TAG, "Analysis: Active=%d, Strong=%d, BLE_adv=%d, Est=%d", 
               active_channels, strong_signals, ble_adv_channels, app->estimated_devices);
}

static void bt_real_scan(BtTestApp* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->scanning = true;
    app->device_found = false;
    strcpy(app->status, "RF Scan...");
    furi_mutex_release(app->mutex);
    
    view_port_update(app->view_port);
    furi_delay_ms(300);
    
    bool found_activity = false;
    int strong_signals = 0;
    
    // Сканируем ВСЕ 40 BLE каналов + дополнительные точки
    int scan_points[] = {
        // BLE Advertising каналы
        37, 38, 39,
        // BLE Data каналы (выборочно)
        0, 5, 10, 15, 20, 25, 30, 36,
        // Дополнительные частоты для классического BT
        12, 18, 24, 33, 39, 45, 51, 57, 63, 69, 75
    };
    int total_points = sizeof(scan_points) / sizeof(scan_points[0]);
    
    // Предварительная калибровка - находим тихий канал для базового уровня шума
    float noise_floor = -100.0f;
    for(int cal = 0; cal < 3; cal++) {
        int quiet_channel = 25; // Предполагаем что этот канал обычно тихий
        furi_hal_bt_start_packet_rx(quiet_channel, 1);
        furi_delay_ms(30);
        float rssi = furi_hal_bt_get_rssi();
        furi_hal_bt_stop_packet_test();
        
        if(rssi > noise_floor) noise_floor = rssi;
        furi_delay_ms(20);
    }
    
    float detection_threshold = noise_floor + 8.0f; // Порог выше шума
    if(detection_threshold > -75.0f) detection_threshold = -75.0f; // Максимальный порог
    
    FURI_LOG_I(TAG, "Noise floor: %.1f dB, Threshold: %.1f dB", 
               (double)noise_floor, (double)detection_threshold);
    
    // Основное сканирование
    for(int i = 0; i < total_points; i++) {
        if(!view_port_is_enabled(app->view_port)) break;
        
        int channel = scan_points[i];
        
        // Показываем прогресс
        if(i % 5 == 0) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            snprintf(app->status, sizeof(app->status), "Scan %d/%d", i + 1, total_points);
            furi_mutex_release(app->mutex);
            view_port_update(app->view_port);
        }
        
        // Сканируем точку
        furi_hal_bt_start_packet_rx(channel, 1);
        furi_delay_ms(40); // Больше времени для стабильного измерения
        
        // Несколько замеров для надежности
        float rssi_sum = 0;
        int valid_samples = 0;
        
        for(int sample = 0; sample < 4; sample++) {
            float current_rssi = furi_hal_bt_get_rssi();
            if(current_rssi > -150.0f && current_rssi < -40.0f) {
                rssi_sum += current_rssi;
                valid_samples++;
            }
            furi_delay_ms(8);
        }
        
        float avg_rssi = (valid_samples > 0) ? (rssi_sum / valid_samples) : -100.0f;
        furi_hal_bt_stop_packet_test();
        
        FURI_LOG_D(TAG, "Point %d (ch %d): %.1f dB", i, channel, (double)avg_rssi);
        
        // Регистрируем активность если выше порога
        if(avg_rssi > detection_threshold) {
            found_activity = true;
            
            // Нормализуем channel для хранения (0-39 для BLE, 40+ для других)
            int store_channel = (channel < MAX_CHANNELS) ? channel : 39;
            
            app->channel_activity[store_channel]++;
            app->channel_rssi[store_channel] = avg_rssi;
            
            if(avg_rssi > -75.0f) {
                strong_signals++;
                FURI_LOG_I(TAG, "STRONG SIGNAL! Ch %d: %.1f dB", channel, (double)avg_rssi);
            }
        }
        
        furi_delay_ms(25);
    }
    
    // Анализируем паттерны
    analyze_patterns(app);
    
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->scanning = false;
    app->scan_count++;
    
    if(found_activity) {
        if(app->estimated_devices > 0) {
            snprintf(app->status, sizeof(app->status), "%d BT devices", app->estimated_devices);
            app->device_found = true;
        } else {
            strcpy(app->status, "RF signals");
            app->device_found = true;
        }
    } else {
        strcpy(app->status, "No BT devices");
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
    
    canvas_draw_str(canvas, 2, 10, "BT/RF Scanner");
    canvas_draw_line(canvas, 0, 12, 127, 12);
    
    canvas_set_font(canvas, FontSecondary);
    
    // Статус
    canvas_draw_str(canvas, 2, 24, app->status);
    
    // Счетчик
    char scan_info[16];
    snprintf(scan_info, sizeof(scan_info), "#%d", app->scan_count);
    canvas_draw_str(canvas, 110, 24, scan_info);
    
    // Контент
    if(app->scanning) {
        canvas_draw_str(canvas, 2, 36, "Scanning...");
        canvas_draw_str(canvas, 2, 46, "21 RF points");
    } else if(app->device_found) {
        if(app->estimated_devices > 0) {
            char dev_str[24];
            snprintf(dev_str, sizeof(dev_str), "Found: %d devices", app->estimated_devices);
            canvas_draw_str(canvas, 2, 36, dev_str);
        } else {
            canvas_draw_str(canvas, 2, 36, "RF activity");
        }
        canvas_draw_str(canvas, 2, 46, app->pattern_info);
    } else {
        canvas_draw_str(canvas, 2, 36, "No Bluetooth");
        canvas_draw_str(canvas, 2, 46, "devices found");
    }
    
    // Индикаторы
    if(app->scan_count > 0) {
        int active_dots = 0;
        for(int i = 37; i <= 39; i++) {
            if(app->channel_activity[i] > 0) active_dots++;
        }
        
        canvas_draw_str(canvas, 70, 36, "BLE:");
        for(int i = 0; i < 3; i++) {
            int x_pos = 90 + i * 10;
            if(i < active_dots) {
                canvas_draw_box(canvas, x_pos, 34, 6, 6);
            } else {
                canvas_draw_frame(canvas, x_pos, 34, 6, 6);
            }
        }
    }
    
    // Управление
    canvas_draw_line(canvas, 0, 52, 127, 52);
    canvas_draw_str(canvas, 2, 60, "OK=Scan");
    canvas_draw_str(canvas, 70, 60, "Back=Exit");
    
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
    
    // Инициализация
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
    
    FURI_LOG_I(TAG, "BT/RF Scanner started");
    
    while(view_port_is_enabled(app->view_port)) {
        view_port_update(app->view_port);
        furi_delay_ms(50);
    }
    
    bt_test_app_free(app);
    FURI_LOG_I(TAG, "BT/RF Scanner stopped");
    return 0;
}
