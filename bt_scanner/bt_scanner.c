#include "bt_scanner.h"

static const NotificationSequence sequence_scan_start = {
    &message_green_255,
    &message_delay_50,
    &message_green_0,
    &message_delay_50,
    &message_green_255,
    &message_delay_50,
    &message_green_0,
    NULL,
};

static const NotificationSequence sequence_device_found = {
    &message_blue_255,
    &message_delay_25,
    &message_blue_0,
    &message_delay_25,
    &message_blue_255,
    &message_delay_25,
    &message_blue_0,
    NULL,
};

static const NotificationSequence sequence_scan_stop = {
    &message_red_255,
    &message_delay_100,
    &message_red_0,
    &message_delay_100,
    &message_red_255,
    &message_delay_100,
    &message_red_0,
    NULL,
};

// Генерация псевдо-MAC на основе канала и RSSI
static void generate_pseudo_mac(uint8_t channel, float rssi, uint8_t* mac) {
    uint32_t seed = (uint32_t)(channel * 1000 + rssi * 100);
    for(int i = 0; i < 6; i++) {
        mac[i] = (seed >> (i * 4)) & 0xFF;
        mac[i] ^= (i * 37 + channel * 3);
    }
}

// Определение типа устройства по каналу и характеристикам сигнала
static BTDeviceType detect_device_type(uint8_t channel, float rssi, float stability) {
    UNUSED(rssi);
    UNUSED(stability);
    
    // BLE использует каналы 37, 38, 39 для advertising
    if(channel >= 37 && channel <= 39) {
        return DeviceTypeBLE;
    }
    // Классический Bluetooth использует определенные диапазоны
    else if(channel <= 10) {
        return DeviceTypeClassic;
    }
    // BLE Data каналы
    else if(channel <= 36) {
        return DeviceTypeBRE;
    }
    return DeviceTypeUnknown;
}

// Генерация псевдо-имени устройства
static void generate_device_name(BTDevice* device, uint8_t channel) {
    UNUSED(channel);
    
    const char* prefixes[] = {"Phone", "Speaker", "Watch", "Headset", "Tracker", "Keyboard", "Mouse"};
    const char* suffixes[] = {"Pro", "Max", "Mini", "Plus", "Lite", "Air", "Ultra"};
    
    uint32_t seed = (device->mac[0] << 8) | device->mac[3];
    const char* prefix = prefixes[seed % (sizeof(prefixes) / sizeof(prefixes[0]))];
    const char* suffix = suffixes[(seed >> 4) % (sizeof(suffixes) / sizeof(suffixes[0]))];
    
    if(device->type == DeviceTypeBLE) {
        snprintf(device->name, sizeof(device->name), "BLE_%s_%s", prefix, suffix);
    } else if(device->type == DeviceTypeClassic) {
        snprintf(device->name, sizeof(device->name), "BT_%s_%s", prefix, suffix);
    } else if(device->type == DeviceTypeBRE) {
        snprintf(device->name, sizeof(device->name), "BR_%s_%s", prefix, suffix);
    } else {
        snprintf(device->name, sizeof(device->name), "RF_%s_%s", prefix, suffix);
    }
}

// Поиск устройства по MAC
static BTDevice* find_device_by_mac(BtScannerState* state, uint8_t* mac) {
    for(uint16_t i = 0; i < state->device_count; i++) {
        if(memcmp(state->devices[i].mac, mac, 6) == 0) {
            return &state->devices[i];
        }
    }
    return NULL;
}

// Добавление нового устройства
static void add_new_device(BtScannerState* state, uint8_t channel, float rssi) {
    if(state->device_count >= MAX_DEVICES) return;
    
    BTDevice new_device = {0};
    generate_pseudo_mac(channel, rssi, new_device.mac);
    
    new_device.rssi = (int8_t)rssi;
    new_device.first_seen = furi_get_tick();
    new_device.last_seen = new_device.first_seen;
    new_device.packet_count = 1;
    new_device.type = detect_device_type(channel, rssi, 1.0f);
    new_device.signal_strength = (uint8_t)CLAMP((rssi + 100) * 2, 0, 100);
    
    // Отметить активность на канале
    memset(new_device.channels, 0, sizeof(new_device.channels));
    new_device.channels[channel] = 1;
    
    generate_device_name(&new_device, channel);
    
    furi_mutex_acquire(state->mutex, FuriWaitForever);
    state->devices[state->device_count] = new_device;
    state->device_count++;
    state->new_device_found = true;
    state->total_packets++;
    state->channel_activity[channel]++;
    
    // Обновление статистики RSSI
    if(rssi > state->max_rssi) state->max_rssi = rssi;
    if(rssi < state->min_rssi) state->min_rssi = rssi;
    
    furi_mutex_release(state->mutex);
    
    notification_message(state->ctx.notification, &sequence_device_found);
}

// Обновление существующего устройства
static void update_existing_device(BtScannerState* state, BTDevice* device, uint8_t channel, float rssi) {
    furi_mutex_acquire(state->mutex, FuriWaitForever);
    
    device->last_seen = furi_get_tick();
    device->packet_count++;
    
    // Усреднение RSSI
    device->rssi = (device->rssi + (int8_t)rssi) / 2;
    device->signal_strength = (uint8_t)CLAMP((device->rssi + 100) * 2, 0, 100);
    
    // Обновление активности каналов
    device->channels[channel]++;
    
    state->total_packets++;
    state->channel_activity[channel]++;
    
    // Обновление статистики RSSI
    if(rssi > state->max_rssi) state->max_rssi = rssi;
    if(rssi < state->min_rssi) state->min_rssi = rssi;
    
    furi_mutex_release(state->mutex);
}

// Сканирование одного канала
static void scan_channel(BtScannerState* state, uint8_t channel) {
    if(!state->scanning) return;
    
    // Настройка приемника на канал
    furi_hal_bt_start_packet_rx(channel, 1);
    furi_delay_ms(15); // Короткая задержка для стабилизации
    
    // Получение RSSI
    float rssi = furi_hal_bt_get_rssi();
    
    // Остановка теста
    furi_hal_bt_stop_packet_test();
    
    // Проверка наличия сигнала выше порога
    if(rssi > RSSI_THRESHOLD) {
        FURI_LOG_D(TAG, "Signal detected on channel %d: %.1f dB", channel, (double)rssi);
        
        // Генерация уникального MAC для этого сигнала
        uint8_t pseudo_mac[6];
        generate_pseudo_mac(channel, rssi, pseudo_mac);
        
        // Поиск существующего устройства
        BTDevice* existing_device = find_device_by_mac(state, pseudo_mac);
        
        if(existing_device) {
            update_existing_device(state, existing_device, channel, rssi);
        } else {
            add_new_device(state, channel, rssi);
        }
    }
    
    furi_delay_ms(5); // Задержка между каналами
}

// Запуск сканирования
void bt_scanner_start_scan(BtScannerState* state) {
    if(state->scanning) return;
    
    FURI_LOG_I(TAG, "Starting RF spectrum scan");
    
    furi_mutex_acquire(state->mutex, FuriWaitForever);
    state->device_count = 0;
    state->scanning = true;
    state->scan_start_time = furi_get_tick();
    state->new_device_found = false;
    state->total_packets = 0;
    state->max_rssi = -120.0f;
    state->min_rssi = 0.0f;
    memset(state->channel_activity, 0, sizeof(state->channel_activity));
    furi_mutex_release(state->mutex);
    
    // Сохраняем текущее состояние Bt (если доступно)
    // Bt* bt = furi_record_open(RECORD_BT); // Пока закомментируем, т.к. RECORD_BT недоступен
    // if(bt) {
    //     bt_disconnect(bt);
    // }
    furi_hal_bt_reinit();
    
    notification_message(state->ctx.notification, &sequence_scan_start);
    FURI_LOG_I(TAG, "RF spectrum scan started");
}

// Остановка сканирования
void bt_scanner_stop_scan(BtScannerState* state) {
    if(!state->scanning) return;
    
    FURI_LOG_I(TAG, "Stopping RF spectrum scan");
    state->scanning = false;
    
    // Восстанавливаем нормальную работу Bt (если доступно)
    // Bt* bt = furi_record_open(RECORD_BT); // Пока закомментируем
    // if(bt) {
    //     bt_profile_restore_default(bt);
    //     furi_record_close(RECORD_BT);
    // }
    
    notification_message(state->ctx.notification, &sequence_scan_stop);
    FURI_LOG_I(TAG, "RF spectrum scan stopped. Found %d devices", state->device_count);
}

// Сортировка устройств по RSSI
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
    
    // Заголовок и статус
    if(state->scanning) {
        uint32_t elapsed = (furi_get_tick() - state->scan_start_time) / 1000;
        // uint32_t remaining = (SCAN_DURATION_MS / 1000) - elapsed; // Убрали неиспользуемую переменную
        
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
    snprintf(stats, sizeof(stats), "Devices: %d", state->device_count);
    canvas_draw_str(canvas, 80, 22, stats);
    
    canvas_draw_line(canvas, 0, 25, 127, 25);
    
    // Список устройств
    if(state->device_count > 0) {
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
            
            // Индикатор сигнала
            uint8_t bars = CLAMP(device->signal_strength / 20, 1, 5);
            for(uint8_t b = 0; b < bars; b++) {
                canvas_draw_box(canvas, 2 + (b * 2), y_pos - 3 - (b * 2), 1, 3 + (b * 2));
            }
            
            // Информация об устройстве
            char device_info[32];
            const char* type_str = "";
            switch(device->type) {
            case DeviceTypeBLE: type_str = "BLE"; break;
            case DeviceTypeClassic: type_str = "BT"; break;
            case DeviceTypeBRE: type_str = "BR/EDR"; break;
            default: type_str = "RF"; break;
            }
            
            snprintf(device_info, sizeof(device_info), "%-8s %ddB", device->name, device->rssi);
            canvas_draw_str(canvas, 15, y_pos, device_info);
            
            // Тип устройства
            canvas_draw_str(canvas, 90, y_pos, type_str);
            
            if(i == state->selected_index) {
                canvas_set_color(canvas, ColorBlack);
            }
            
            y_pos += 10;
        }
        
        // Индикатор скролла
        if(state->device_count > 3) {
            canvas_draw_str(canvas, 120, 35, "↓");
        }
    } else {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 10, 45, "No devices found");
        if(!state->scanning) {
            canvas_draw_str(canvas, 10, 55, "Start scan to discover");
        }
    }
    
    // Подсказки
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 60, "OK:Scan/Details  Back:Menu");
}

// Отрисовка спектра каналов
static void draw_spectrum_view(Canvas* canvas, BtScannerState* state) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "RF Spectrum");
    canvas_draw_line(canvas, 0, 12, 127, 12);
    
    canvas_set_font(canvas, FontSecondary);
    
    // Отрисовка активности каналов
    uint8_t max_activity = 0;
    for(int i = 0; i < CHANNEL_COUNT; i++) {
        if(state->channel_activity[i] > max_activity) {
            max_activity = state->channel_activity[i];
        }
    }
    
    if(max_activity > 0) {
        for(int i = 0; i < CHANNEL_COUNT; i++) {
            uint8_t height = (state->channel_activity[i] * 20) / max_activity;
            if(height > 0) {
                canvas_draw_box(canvas, i * 3 + 2, 30 - height, 2, height);
            }
            // Номера каналов для BLE advertising
            if(i == 37 || i == 38 || i == 39) {
                canvas_draw_box(canvas, i * 3 + 1, 32, 4, 2);
            }
        }
        
        // Легенда
        canvas_draw_str(canvas, 2, 45, "BLE Adv");
        canvas_draw_box(canvas, 40, 44, 4, 2);
    }
    
    // Статистика
    char stats[64];
    snprintf(stats, sizeof(stats), "Packets: %lu  RSSI: %.1f/%.1f", 
             state->total_packets, (double)state->max_rssi, (double)state->min_rssi);
    canvas_draw_str(canvas, 2, 55, stats);
    
    canvas_draw_str(canvas, 2, 75, "Back:Return");
}

// Детали устройства
static void draw_device_details(Canvas* canvas, BtScannerState* state) {
    if(state->device_count == 0) return;
    
    BTDevice* device = &state->devices[state->selected_index];
    char buffer[64];
    
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Device Details");
    canvas_draw_line(canvas, 0, 12, 127, 12);
    
    canvas_set_font(canvas, FontSecondary);
    
    // MAC адрес
    snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
            device->mac[0], device->mac[1], device->mac[2],
            device->mac[3], device->mac[4], device->mac[5]);
    canvas_draw_str(canvas, 2, 25, "MAC:");
    canvas_draw_str(canvas, 30, 25, buffer);
    
    // Имя и тип
    canvas_draw_str(canvas, 2, 37, "Name:");
    canvas_draw_str(canvas, 30, 37, device->name);
    
    const char* type_str = "Unknown";
    switch(device->type) {
    case DeviceTypeBLE: type_str = "BLE"; break;
    case DeviceTypeClassic: type_str = "Classic BT"; break;
    case DeviceTypeBRE: type_str = "BR/EDR"; break;
    default: type_str = "Unknown"; break;
    }
    snprintf(buffer, sizeof(buffer), "Type: %s", type_str);
    canvas_draw_str(canvas, 2, 49, buffer);
    
    // RSSI и сила сигнала
    snprintf(buffer, sizeof(buffer), "RSSI: %d dBm", device->rssi);
    canvas_draw_str(canvas, 70, 49, buffer);
    
    snprintf(buffer, sizeof(buffer), "Signal: %d%%", device->signal_strength);
    canvas_draw_str(canvas, 2, 61, buffer);
    
    // Пакеты и время
    snprintf(buffer, sizeof(buffer), "Packets: %d", device->packet_count);
    canvas_draw_str(canvas, 70, 61, buffer);
    
    canvas_draw_str(canvas, 2, 75, "Back:Return  OK:Save Log");
}

// Сохранение лога
bool bt_scanner_save_log(BtScannerState* state) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    FuriString* file_path = furi_string_alloc();
    
    furi_string_printf(file_path, "/ext/bt_scan_%lu.txt", furi_get_tick());
    
    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, ".txt", NULL);
    options.base_path = "/ext";
    
    bool success = false;
    if(dialog_file_browser_show(dialogs, file_path, file_path, &options)) {
        File* file = storage_file_alloc(storage);
        
        if(storage_file_open(file, furi_string_get_cstr(file_path), FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            char buffer[256];
            
            furi_mutex_acquire(state->mutex, FuriWaitForever);
            
            // Заголовок
            storage_file_write(file, "=== FLIPPER ZERO RF SCANNER LOG ===\n", 37);
            snprintf(buffer, sizeof(buffer), 
                    "Scan time: %lu ms\n"
                    "Devices found: %d\n"
                    "Total packets: %lu\n"
                    "Max RSSI: %.1f dB\n"
                    "Min RSSI: %.1f dB\n\n", 
                    furi_get_tick(), state->device_count, state->total_packets,
                    (double)state->max_rssi, (double)state->min_rssi);
            storage_file_write(file, buffer, strlen(buffer));
            
            // Устройства
            for(uint16_t i = 0; i < state->device_count; i++) {
                BTDevice* dev = &state->devices[i];
                const char* type_str = "Unknown";
                switch(dev->type) {
                case DeviceTypeBLE: type_str = "BLE"; break;
                case DeviceTypeClassic: type_str = "Classic BT"; break;
                case DeviceTypeBRE: type_str = "BR/EDR"; break;
                default: type_str = "Unknown"; break;
                }
                
                snprintf(buffer, sizeof(buffer),
                    "Device %d:\n"
                    "  MAC: %02X:%02X:%02X:%02X:%02X:%02X\n"
                    "  Name: %s\n"
                    "  Type: %s\n"
                    "  RSSI: %d dBm\n"
                    "  Signal: %d%%\n"
                    "  Packets: %d\n"
                    "  First seen: %lu ms\n"
                    "  Last seen: %lu ms\n\n",
                    i + 1, dev->mac[0], dev->mac[1], dev->mac[2],
                    dev->mac[3], dev->mac[4], dev->mac[5],
                    dev->name, type_str, dev->rssi, dev->signal_strength,
                    dev->packet_count, dev->first_seen, dev->last_seen);
                storage_file_write(file, buffer, strlen(buffer));
            }
            
            // Активность каналов
            storage_file_write(file, "=== CHANNEL ACTIVITY ===\n", 25);
            for(int i = 0; i < CHANNEL_COUNT; i++) {
                if(state->channel_activity[i] > 0) {
                    snprintf(buffer, sizeof(buffer), "Channel %2d: %3d packets\n", 
                            i, state->channel_activity[i]);
                    storage_file_write(file, buffer, strlen(buffer));
                }
            }
            
            furi_mutex_release(state->mutex);
            storage_file_close(file);
            success = true;
            
            notification_message(state->ctx.notification, &sequence_success);
            FURI_LOG_I(TAG, "Log saved to: %s", furi_string_get_cstr(file_path));
        }
        storage_file_free(file);
    }
    
    furi_string_free(file_path);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    return success;
}

// Основная отрисовка
void bt_scanner_draw_callback(Canvas* canvas, void* _ctx) {
    BtScannerState* state = (BtScannerState*)_ctx;
    if(furi_mutex_acquire(state->mutex, 100) != FuriStatusOk) return;
    
    if(state->show_spectrum) {
        draw_spectrum_view(canvas, state);
    } else if(state->show_details) {
        draw_device_details(canvas, state);
    } else {
        draw_main_screen(canvas, state);
    }
    
    furi_mutex_release(state->mutex);
}

// Обработка ввода
void bt_scanner_input_callback(InputEvent* input, void* _ctx) {
    BtScannerState* state = (BtScannerState*)_ctx;

    if(furi_mutex_acquire(state->mutex, 100) != FuriStatusOk) return;

    if(state->show_spectrum) {
        if(input->type == InputTypeShort && input->key == InputKeyBack) {
            state->show_spectrum = false;
        }
    } else if(state->show_details) {
        if(input->type == InputTypeShort) {
            switch(input->key) {
            case InputKeyBack:
                state->show_details = false;
                break;
            case InputKeyOk:
                bt_scanner_save_log(state);
                state->show_details = false;
                break;
            default:
                break;
            }
        }
    } else {
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
            case InputKeyLeft:
                if(state->device_count > 0 && !state->scanning) {
                    state->show_spectrum = true;
                }
                break;
            case InputKeyOk:
                if(state->scanning) {
                    bt_scanner_stop_scan(state);
                } else if(state->device_count > 0) {
                    state->show_details = true;
                } else {
                    bt_scanner_start_scan(state);
                }
                break;
            case InputKeyBack:
                if(state->scanning) {
                    bt_scanner_stop_scan(state);
                }
                break;
            default:
                break;
            }
        }
    }

    furi_mutex_release(state->mutex);
}

// Инициализация
BtScannerState* bt_scanner_alloc() {
    BtScannerState* state = malloc(sizeof(BtScannerState));
    memset(state, 0, sizeof(BtScannerState));
    
    state->ctx.gui = furi_record_open(RECORD_GUI);
    state->ctx.notification = furi_record_open(RECORD_NOTIFICATION);
    state->ctx.storage = furi_record_open(RECORD_STORAGE);
    state->ctx.dialogs = furi_record_open(RECORD_DIALOGS);
    
    state->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    
    state->ctx.view_port = view_port_alloc();
    view_port_draw_callback_set(state->ctx.view_port, bt_scanner_draw_callback, state);
    view_port_input_callback_set(state->ctx.view_port, bt_scanner_input_callback, state);
    gui_add_view_port(state->ctx.gui, state->ctx.view_port, GuiLayerFullscreen);
    
    state->min_rssi = 0.0f;
    
    return state;
}

// Очистка
void bt_scanner_free(BtScannerState* state) {
    if(!state) return;
    
    if(state->scanning) {
        bt_scanner_stop_scan(state);
    }
    
    gui_remove_view_port(state->ctx.gui, state->ctx.view_port);
    view_port_free(state->ctx.view_port);
    furi_mutex_free(state->mutex);
    
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    
    free(state);
}

// Главная функция приложения
int32_t bt_scanner_app(void* p) {
    UNUSED(p);
    
    BtScannerState* state = bt_scanner_alloc();
    FURI_LOG_I(TAG, "BT Scanner started");
    
    // Основной цикл сканирования
    while(true) {
        view_port_update(state->ctx.view_port);
        
        if(state->scanning) {
            // Сканирование всех каналов
            for(uint8_t channel = 0; channel < CHANNEL_COUNT && state->scanning; channel++) {
                scan_channel(state, channel);
            }
            
            // Проверка времени сканирования
            uint32_t elapsed = furi_get_tick() - state->scan_start_time;
            if(elapsed >= SCAN_DURATION_MS) {
                bt_scanner_stop_scan(state);
            }
        }
        
        furi_delay_ms(10);
    }
    
    bt_scanner_free(state);
    return 0;
}
