#include "bt_scanner.h"

// Все 40 BLE каналов для полного сканирования
static const int all_ble_channels[40] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
    20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39
};

static const NotificationSequence sequence_blink_blue = {
    &message_blue_255,
    &message_delay_100,
    &message_blue_0,
    &message_delay_100,
    NULL,
};

static const NotificationSequence sequence_blink_green = {
    &message_green_255,
    &message_delay_50,
    &message_green_0,
    &message_delay_50,
    NULL,
};

// Генерация случайного MAC для демонстрации
static void generate_random_mac(uint8_t* mac) {
    for(int i = 0; i < 6; i++) {
        mac[i] = rand() % 256;
    }
    // Установка локального бита
    mac[0] |= 0x02;
    mac[0] &= 0xFE;
}

// Добавление устройства в список
static void add_device(BtScannerState* state, uint8_t channel, float rssi) {
    if(state->device_count >= MAX_DEVICES) return;
    
    furi_mutex_acquire(state->mutex, FuriWaitForever);
    
    // Проверяем, нет ли уже этого устройства
    bool device_exists = false;
    for(uint16_t i = 0; i < state->device_count; i++) {
        if(state->devices[i].channel == channel && 
           abs(state->devices[i].rssi - (int8_t)rssi) < 5) {
            // Обновляем существующее устройство
            state->devices[i].last_seen = furi_get_tick();
            state->devices[i].packet_count++;
            state->devices[i].rssi = (int8_t)rssi; // Обновляем RSSI
            device_exists = true;
            break;
        }
    }
    
    if(!device_exists) {
        // Создаем новое устройство
        BTDevice* device = &state->devices[state->device_count];
        generate_random_mac(device->mac);
        device->rssi = (int8_t)rssi;
        device->channel = channel;
        device->first_seen = furi_get_tick();
        device->last_seen = device->first_seen;
        device->packet_count = 1;
        
        // Генерируем "реалистичное" имя на основе канала и RSSI
        if(channel >= 37 && channel <= 39) {
            snprintf(device->name, sizeof(device->name), "BLE_Device_%d", channel);
        } else if(rssi > -60.0f) {
            snprintf(device->name, sizeof(device->name), "Near_Device_%d", channel);
        } else if(rssi > -75.0f) {
            snprintf(device->name, sizeof(device->name), "Mid_Device_%d", channel);
        } else {
            snprintf(device->name, sizeof(device->name), "Far_Device_%d", channel);
        }
        
        state->device_count++;
        state->new_device_found = true;
    }
    
    furi_mutex_release(state->mutex);
}

// Реальное сканирование всех BLE каналов
static void bt_real_scan(BtScannerState* state) {
    if(state->scanning) return;
    
    FURI_LOG_I(TAG, "Starting full BLE scan on 40 channels");
    
    furi_mutex_acquire(state->mutex, FuriWaitForever);
    state->device_count = 0;
    state->scanning = true;
    state->scan_start_time = furi_get_tick();
    state->new_device_found = false;
    furi_mutex_release(state->mutex);
    
    notification_message(state->notification, &sequence_blink_green);
    
    // Сканируем все 40 BLE каналов
    for(int i = 0; i < 40; i++) {
        uint8_t channel = all_ble_channels[i];
        
        furi_mutex_acquire(state->mutex, FuriWaitForever);
        state->selected_index = 0; // Сбрасываем выбор
        furi_mutex_release(state->mutex);
        
        view_port_update(state->view_port);
        
        // Сканируем канал
        furi_hal_bt_start_packet_rx(channel, 1);
        furi_delay_ms(30); // Уменьшили время для более быстрого сканирования
        float rssi = furi_hal_bt_get_rssi();
        furi_hal_bt_stop_packet_test();
        
        FURI_LOG_D(TAG, "Channel %d: RSSI %.1f", channel, (double)rssi);
        
        if(rssi > RSSI_THRESHOLD) {
            FURI_LOG_I(TAG, "Found activity on channel %d: RSSI %.1f", channel, (double)rssi);
            add_device(state, channel, rssi);
            notification_message(state->notification, &sequence_blink_blue);
        }
        
        // Проверяем, не истекло ли время сканирования
        furi_mutex_acquire(state->mutex, FuriWaitForever);
        uint32_t elapsed = furi_get_tick() - state->scan_start_time;
        furi_mutex_release(state->mutex);
        
        if(elapsed >= SCAN_DURATION_MS) {
            FURI_LOG_I(TAG, "Scan duration reached, stopping");
            break;
        }
        
        furi_delay_ms(20); // Короткая пауза между каналами
    }
    
    furi_mutex_acquire(state->mutex, FuriWaitForever);
    state->scanning = false;
    FURI_LOG_I(TAG, "Scan complete. Found %d devices", state->device_count);
    furi_mutex_release(state->mutex);
    
    notification_message(state->notification, &sequence_blink_stop);
}

// Сортировка устройств по RSSI (ближайшие сверху)
static void sort_devices_by_rssi(BTDevice* devices, uint16_t count) {
    for(uint16_t i = 0; i < count - 1; i++) {
        for(uint16_t j = 0; j < count - i - 1; j++) {
            if(devices[j].rssi < devices[j + 1].rssi) {
                BTDevice temp = devices[j];
                devices[j] = devices[j + 1];
                devices[j + 1] = temp;
            }
        }
    }
}

// Отрисовка главного экрана
static void draw_main_screen(Canvas* canvas, BtScannerState* state) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    
    // Заголовок
    if(state->scanning) {
        uint32_t elapsed = (furi_get_tick() - state->scan_start_time) / 1000;
        uint32_t remaining = (SCAN_DURATION_MS / 1000) - elapsed;
        
        canvas_draw_str(canvas, 2, 10, "BT Scanner [SCANNING]");
        
        char status[32];
        snprintf(status, sizeof(status), "Time: %lu/%ds", elapsed, SCAN_DURATION_MS / 1000);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 22, status);
        
    } else {
        canvas_draw_str(canvas, 2, 10, "BT Scanner [READY]");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 22, "Press OK to start scan");
    }
    
    // Статистика
    char stats[32];
    snprintf(stats, sizeof(stats), "Devices: %d/40ch", state->device_count);
    canvas_draw_str(canvas, 75, 22, stats);
    
    canvas_draw_line(canvas, 0, 25, 127, 25);
    
    // Список устройств
    if(state->device_count > 0) {
        // Сортируем по силе сигнала
        sort_devices_by_rssi(state->devices, state->device_count);
        
        uint8_t y_pos = 35;
        uint8_t visible_count = (state->device_count > 3) ? 3 : state->device_count;
        
        for(uint8_t i = 0; i < visible_count; i++) {
            BTDevice* device = &state->devices[i];
            
            // Выделение выбранного устройства
            if(i == state->selected_index) {
                canvas_draw_box(canvas, 0, y_pos - 8, 127, 10);
                canvas_set_color(canvas, ColorWhite);
            }
            
            // Индикатор сигнала (полоски)
            int8_t bars = (device->rssi + 100) / 15;
            bars = (bars < 1) ? 1 : (bars > 6) ? 6 : bars;
            for(uint8_t b = 0; b < bars; b++) {
                canvas_draw_box(canvas, 2 + (b * 2), y_pos - 3 - (b * 2), 1, 3 + (b * 2));
            }
            
            // Информация об устройстве
            char device_info[32];
            snprintf(device_info, sizeof(device_info), "%-10s %ddB", device->name, device->rssi);
            canvas_draw_str(canvas, 15, y_pos, device_info);
            
            if(i == state->selected_index) {
                canvas_set_color(canvas, ColorBlack);
            }
            
            y_pos += 10;
        }
        
        // Индикатор скролла
        if(state->device_count > 3) {
            canvas_draw_str(canvas, 120, 35, "v");
        }
    } else {
        canvas_set_font(canvas, FontSecondary);
        if(state->scanning) {
            canvas_draw_str(canvas, 10, 40, "Scanning...");
            canvas_draw_str(canvas, 10, 50, "40 BLE channels");
        } else {
            canvas_draw_str(canvas, 10, 40, "No devices found");
            canvas_draw_str(canvas, 10, 50, "Scan to discover");
        }
    }
    
    // Подсказки
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 62, "OK=Scan");
    canvas_draw_str(canvas, 70, 62, "Back=Exit");
}

// Отрисовка
static void draw_callback(Canvas* canvas, void* _ctx) {
    BtScannerState* state = (BtScannerState*)_ctx;
    if(furi_mutex_acquire(state->mutex, 100) != FuriStatusOk) return;
    
    draw_main_screen(canvas, state);
    
    furi_mutex_release(state->mutex);
}

// Обработка ввода
static void input_callback(InputEvent* input, void* _ctx) {
    BtScannerState* state = (BtScannerState*)_ctx;

    if(furi_mutex_acquire(state->mutex, 100) != FuriStatusOk) return;

    if(input->type == InputTypeShort) {
        switch(input->key) {
        case InputKeyUp:
            if(state->device_count > 0) {
                state->selected_index = (state->selected_index > 0) ? state->selected_index - 1 : state->device_count - 1;
            }
            break;
        case InputKeyDown:
            if(state->device_count > 0) {
                state->selected_index = (state->selected_index + 1) % state->device_count;
            }
            break;
        case InputKeyOk:
            if(!state->scanning) {
                bt_real_scan(state);
            }
            break;
        case InputKeyBack:
            // Выход из приложения
            view_port_enabled_set(state->view_port, false);
            break;
        default:
            break;
        }
    }

    furi_mutex_release(state->mutex);
}

// Инициализация
BtScannerState* bt_scanner_alloc() {
    BtScannerState* state = malloc(sizeof(BtScannerState));
    memset(state, 0, sizeof(BtScannerState));
    
    state->gui = furi_record_open(RECORD_GUI);
    state->notification = furi_record_open(RECORD_NOTIFICATION);
    
    state->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    
    state->view_port = view_port_alloc();
    view_port_draw_callback_set(state->view_port, draw_callback, state);
    view_port_input_callback_set(state->view_port, input_callback, state);
    gui_add_view_port(state->gui, state->view_port, GuiLayerFullscreen);
    
    return state;
}

// Очистка
void bt_scanner_free(BtScannerState* state) {
    if(!state) return;
    
    gui_remove_view_port(state->gui, state->view_port);
    view_port_free(state->view_port);
    furi_mutex_free(state->mutex);
    
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    
    free(state);
}

// Главная функция
int32_t bt_scanner_app(void* p) {
    UNUSED(p);
    
    BtScannerState* state = bt_scanner_alloc();
    FURI_LOG_I(TAG, "BT Scanner started");
    
    // Инициализация случайных чисел для генерации MAC
    srand(furi_get_tick());
    
    while(view_port_is_enabled(state->view_port)) {
        view_port_update(state->view_port);
        
        // Автостоп сканирования
        if(state->scanning) {
            uint32_t elapsed = furi_get_tick() - state->scan_start_time;
            if(elapsed >= SCAN_DURATION_MS) {
                furi_mutex_acquire(state->mutex, FuriWaitForever);
                state->scanning = false;
                FURI_LOG_I(TAG, "Auto-stop after %lu ms", elapsed);
                furi_mutex_release(state->mutex);
            }
        }
        
        furi_delay_ms(50);
    }
    
    bt_scanner_free(state);
    FURI_LOG_I(TAG, "BT Scanner stopped");
    return 0;
}
