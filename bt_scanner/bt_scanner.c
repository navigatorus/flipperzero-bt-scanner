#include "bt_scanner.h"

static void analyze_patterns(BtTestApp* app) {
    int ble_adv_channels = 0;    // BLE advertising каналы (37,38,39)
    int ble_data_channels = 0;   // BLE data каналы (0-36)
    int active_channels = 0;
    
    // Анализируем активность по каналам
    for(int i = 0; i < MAX_CHANNELS; i++) {
        if(app->channel_activity[i] > 0) {
            active_channels++;
            
            if(i >= 37 && i <= 39) {
                ble_adv_channels++;
            } else if(i >= 0 && i <= 36) {
                ble_data_channels++;
            }
        }
    }
    
    // Оценка количества устройств ТОЛЬКО если есть реальные сигналы
    if(active_channels == 0) {
        app->estimated_devices = 0;
        strcpy(app->pattern_info, "No activity");
    } else if(ble_adv_channels >= 2) {
        // BLE устройства обычно рекламируются на 2-3 каналах
        app->estimated_devices = ble_adv_channels;
        strcpy(app->pattern_info, "BLE devices");
    } else if(active_channels > 10) {
        // Много активности - возможно классический Bluetooth
        app->estimated_devices = active_channels / 3;
        strcpy(app->pattern_info, "Classic BT");
    } else {
        // Слабые сигналы - вероятно шум
        app->estimated_devices = 0;
        strcpy(app->pattern_info, "Weak signals");
    }
    
    FURI_LOG_I(TAG, "Pattern analysis: BLE_adv=%d, BLE_data=%d, Active=%d, Est_devices=%d", 
               ble_adv_channels, ble_data_channels, active_channels, app->estimated_devices);
}

static void bt_real_scan(BtTestApp* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->scanning = true;
    app->device_found = false;
    strcpy(app->status, "Calibrating...");
    furi_mutex_release(app->mutex);
    
    view_port_update(app->view_port);
    furi_delay_ms(500);
    
    bool found_activity = false;
    
    // ШАГ 1: Калибровка - измеряем уровень шума
    float noise_level = -100.0f;
    for(int cal = 0; cal < 5; cal++) {
        furi_hal_bt_start_packet_rx(37, 1); // Используем один канал для калибровки
        furi_delay_ms(20);
        float current_rssi = furi_hal_bt_get_rssi();
        furi_hal_bt_stop_packet_test();
        
        if(current_rssi > noise_level) {
            noise_level = current_rssi;
        }
        furi_delay_ms(10);
    }
    
    FURI_LOG_I(TAG, "Noise level: %.1f dB", (double)noise_level);
    
    // Устанавливаем порог выше уровня шума
    float detection_threshold = noise_level + 5.0f;
    if(detection_threshold > -85.0f) detection_threshold = -85.0f; // Максимальный порог
    
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    snprintf(app->status, sizeof(app->status), "Scanning...");
    furi_mutex_release(app->mutex);
    view_port_update(app->view_port);
    
    // ШАГ 2: Сканируем все каналы с учетом шума
    for(int channel = 0; channel < MAX_CHANNELS; channel++) {
        if(!view_port_is_enabled(app->view_port)) break;
        
        // Показываем прогресс только для некоторых каналов чтобы не мелькало
        if(channel % 10 == 0) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            snprintf(app->status, sizeof(app->status), "Scan %d/%d", channel + 1, MAX_CHANNELS);
            furi_mutex_release(app->mutex);
            view_port_update(app->view_port);
        }
        
        // Сканируем канал
        furi_hal_bt_start_packet_rx(channel, 1);
        furi_delay_ms(25);
        
        // Берем несколько замеров для фильтрации
        float rssi_sum = 0;
        int valid_samples = 0;
        
        for(int sample = 0; sample < 3; sample++) {
            float current_rssi = furi_hal_bt_get_rssi();
            // Фильтруем явно невалидные значения
            if(current_rssi > -150.0f && current_rssi < -30.0f) {
                rssi_sum += current_rssi;
                valid_samples++;
            }
            furi_delay_ms(5);
        }
        
        float avg_rssi = (valid_samples > 0) ? (rssi_sum / valid_samples) : -100.0f;
        furi_hal_bt_stop_packet_test();
        
        FURI_LOG_D(TAG, "Channel %d: RSSI %.1f (threshold: %.1f)", 
                  channel, (double)avg_rssi, (double)detection_threshold);
        
        // Обновляем статистику канала ТОЛЬКО если сигнал выше порога
        if(avg_rssi > detection_threshold) {
            found_activity = true;
            app->channel_activity[channel]++;
            app->channel_rssi[channel] = avg_rssi;
            FURI_LOG_I(TAG, "REAL SIGNAL! Channel %d: %.1f dB", channel, (double)avg_rssi);
        }
        
        furi_delay_ms(15);
    }
    
    // Анализируем паттерны
    analyze_patterns(app);
    
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->scanning = false;
    app->scan_count++;
    
    if(found_activity && app->estimated_devices > 0) {
        snprintf(app->status, sizeof(app->status), "%d devices", app->estimated_devices);
        app->device_found = true;
    } else {
        strcpy(app->status, "No devices");
        app->device_found = false;
        app->estimated_devices = 0;
        strcpy(app->pattern_info, "Only noise");
    }
    
    furi_mutex_release(app->mutex);
}

static void bt_test_app_draw_callback(Canvas* canvas, void* context) {
    BtTestApp* app = context;
    
    if(furi_mutex_acquire(app->mutex, 100) != FuriStatusOk) return;
    
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    
    // Заголовок
    canvas_draw_str(canvas, 2, 10, "BT Analyzer");
    canvas_draw_line(canvas, 0, 12, 127, 12);
    
    canvas_set_font(canvas, FontSecondary);
    
    // Основной статус
    canvas_draw_str(canvas, 2, 24, app->status);
    
    // Счетчик сканирований
    char scan_info[16];
    snprintf(scan_info, sizeof(scan_info), "#%d", app->scan_count);
    canvas_draw_str(canvas, 110, 24, scan_info);
    
    // Основной контент
    if(app->scanning) {
        canvas_draw_str(canvas, 2, 36, "Scanning...");
        canvas_draw_str(canvas, 2, 46, "Calibrated scan");
    } else if(app->device_found) {
        // Показываем оценку устройств
        char devices_str[20];
        snprintf(devices_str, sizeof(devices_str), "Est: %d devices", app->estimated_devices);
        canvas_draw_str(canvas, 2, 36, devices_str);
        canvas_draw_str(canvas, 2, 46, app->pattern_info);
    } else {
        if(strcmp(app->status, "Press OK to scan") == 0) {
            canvas_draw_str(canvas, 2, 36, "Press OK to");
            canvas_draw_str(canvas, 2, 46, "calibrate & scan");
        } else {
            canvas_draw_str(canvas, 2, 36, "Scan complete");
            canvas_draw_str(canvas, 2, 46, app->pattern_info);
        }
    }
    
    // Визуализация BLE каналов
    if(app->scan_count > 0) {
        canvas_draw_str(canvas, 70, 36, "Ch37-39:");
        
        for(int i = 37; i <= 39; i++) {
            int x_pos = 90 + (i - 37) * 10;
            if(app->channel_activity[i] > 0) {
                canvas_draw_box(canvas, x_pos, 34, 6, 6); // Активный
            } else {
                canvas_draw_frame(canvas, x_pos, 34, 6, 6); // Неактивный
            }
        }
    }
    
    // Подсказки управления
    canvas_draw_line(canvas, 0, 52, 127, 52);
    canvas_draw_str(canvas, 2, 60, "OK=Scan");
    canvas_draw_str(canvas, 70, 60, "Back=Exit");
    
    furi_mutex_release(app->mutex);
}

// Остальные функции без изменений...
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
    
    FURI_LOG_I(TAG, "BT Analyzer started");
    
    while(view_port_is_enabled(app->view_port)) {
        view_port_update(app->view_port);
        furi_delay_ms(50);
    }
    
    bt_test_app_free(app);
    FURI_LOG_I(TAG, "BT Analyzer stopped");
    return 0;
}
