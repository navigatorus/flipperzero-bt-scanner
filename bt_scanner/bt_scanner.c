#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_bt.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <dialogs/dialogs.h>
#include <toolbox/stream/stream.h>
#include <toolbox/stream/file_stream.h>

#define TAG "BtScanner"
#define MAX_DEVICES 100
#define SCAN_DURATION_MS 30000

typedef struct {
    uint8_t mac[6];
    int8_t rssi;
    char name[32];
    uint32_t first_seen;
    uint32_t last_seen;
    uint16_t packet_count;
} BTDevice;

typedef struct {
    ViewPort* view_port;
    Gui* gui;
    NotificationApp* notification;
    Storage* storage;
    DialogsApp* dialogs;
} AppContext;

typedef struct {
    AppContext ctx;
    FuriMutex* mutex;
    
    BTDevice devices[MAX_DEVICES];
    uint16_t device_count;
    bool scanning;
    uint32_t scan_start_time;
    bool new_device_found;
    
    uint16_t selected_index;
    bool show_details;
} BtScannerState;

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

// Callback для реального BLE сканирования
static void bt_scan_callback(void* context, uint32_t event) {
    BtScannerState* state = (BtScannerState*)context;
    
    if(event != BtStatusReady) return;

    // Получаем результаты сканирования
    uint32_t devices_count = bt_get_discovered_devices_count();
    
    furi_mutex_acquire(state->mutex, FuriWaitForever);
    
    for(uint32_t i = 0; i < devices_count && state->device_count < MAX_DEVICES; i++) {
        BTDevice new_device = {0};
        
        // Получаем информацию об устройстве
        const char* name = bt_get_discovered_device_name(i);
        int8_t rssi = bt_get_discovered_device_rssi(i);
        const uint8_t* mac = bt_get_discovered_device_address(i);
        
        if(!mac) continue;
        
        // Проверяем, есть ли уже такое устройство
        bool exists = false;
        for(uint16_t j = 0; j < state->device_count; j++) {
            if(memcmp(state->devices[j].mac, mac, 6) == 0) {
                state->devices[j].rssi = rssi;
                state->devices[j].last_seen = furi_get_tick();
                state->devices[j].packet_count++;
                exists = true;
                break;
            }
        }
        
        if(!exists) {
            memcpy(new_device.mac, mac, 6);
            new_device.rssi = rssi;
            new_device.first_seen = furi_get_tick();
            new_device.last_seen = furi_get_tick();
            new_device.packet_count = 1;
            
            if(name && strlen(name) > 0) {
                strlcpy(new_device.name, name, sizeof(new_device.name));
            } else {
                snprintf(new_device.name, sizeof(new_device.name), 
                        "%02X:%02X:%02X", mac[3], mac[4], mac[5]);
            }
            
            state->devices[state->device_count] = new_device;
            state->device_count++;
            state->new_device_found = true;
            
            notification_message(state->ctx.notification, &sequence_blink_blue);
        }
    }
    
    furi_mutex_release(state->mutex);
}

// Запуск реального сканирования
static void start_scanning(BtScannerState* state) {
    if(state->scanning) return;
    
    FURI_LOG_I(TAG, "Starting REAL BLE scan");
    
    // Очищаем старые устройства
    furi_mutex_acquire(state->mutex, FuriWaitForever);
    state->device_count = 0;
    state->scanning = true;
    state->scan_start_time = furi_get_tick();
    state->new_device_found = false;
    furi_mutex_release(state->mutex);
    
    // Запускаем настоящее BLE сканирование
    bt_set_status_changed_callback(bt_scan_callback, state);
    bt_start_discovery();
    
    notification_message(state->ctx.notification, &sequence_blink_green);
    FURI_LOG_I(TAG, "Real BLE scan started");
}

// Остановка сканирования
static void stop_scanning(BtScannerState* state) {
    if(!state->scanning) return;
    
    FURI_LOG_I(TAG, "Stopping BLE scan");
    bt_stop_discovery();
    bt_set_status_changed_callback(NULL, NULL);
    state->scanning = false;
    
    notification_message(state->ctx.notification, &sequence_blink_stop);
    FURI_LOG_I(TAG, "BLE scan stopped. Found %d devices", state->device_count);
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
            int8_t bars = (device->rssi + 80) / 10; // -80 to 0 dBm
            bars = CLAMP(bars, 1, 6);
            for(uint8_t b = 0; b < bars; b++) {
                canvas_draw_box(canvas, 2 + (b * 2), y_pos - 3 - (b * 2), 1, 3 + (b * 2));
            }
            
            // Информация об устройстве
            char device_info[32];
            snprintf(device_info, sizeof(device_info), "%-12s %ddB", device->name, device->rssi);
            canvas_draw_str(canvas, 15, y_pos, device_info);
            
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
    
    // Имя
    canvas_draw_str(canvas, 2, 37, "Name:");
    canvas_draw_str(canvas, 30, 37, device->name);
    
    // RSSI
    snprintf(buffer, sizeof(buffer), "RSSI: %d dBm", device->rssi);
    canvas_draw_str(canvas, 2, 49, buffer);
    
    // Пакеты
    snprintf(buffer, sizeof(buffer), "Packets: %d", device->packet_count);
    canvas_draw_str(canvas, 70, 49, buffer);
    
    // Сигнал
    canvas_draw_str(canvas, 2, 61, "Signal strength:");
    
    canvas_draw_str(canvas, 2, 75, "Back:Return  OK:Save Log");
}

// Сохранение лога
static bool save_log(BtScannerState* state) {
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
            storage_file_write(file, "=== FLIPPER ZERO BT SCANNER LOG ===\n", 37);
            snprintf(buffer, sizeof(buffer), "Scan time: %lu\nDevices: %d\n\n", 
                    furi_get_tick(), state->device_count);
            storage_file_write(file, buffer, strlen(buffer));
            
            // Устройства
            for(uint16_t i = 0; i < state->device_count; i++) {
                BTDevice* dev = &state->devices[i];
                snprintf(buffer, sizeof(buffer),
                    "Device %d:\n"
                    "  MAC: %02X:%02X:%02X:%02X:%02X:%02X\n"
                    "  Name: %s\n"
                    "  RSSI: %d dBm\n"
                    "  Packets: %d\n\n",
                    i + 1, dev->mac[0], dev->mac[1], dev->mac[2],
                    dev->mac[3], dev->mac[4], dev->mac[5],
                    dev->name, dev->rssi, dev->packet_count);
                storage_file_write(file, buffer, strlen(buffer));
            }
            
            furi_mutex_release(state->mutex);
            storage_file_close(file);
            success = true;
            notification_message(state->ctx.notification, &sequence_success);
        }
        storage_file_free(file);
    }
    
    furi_string_free(file_path);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    return success;
}

// Отрисовка
static void draw_callback(Canvas* canvas, void* _ctx) {
    BtScannerState* state = (BtScannerState*)_ctx;
    if(furi_mutex_acquire(state->mutex, 100) != FuriStatusOk) return;
    
    if(state->show_details) {
        draw_device_details(canvas, state);
    } else {
        draw_main_screen(canvas, state);
    }
    
    furi_mutex_release(state->mutex);
}

// Обработка ввода
static void input_callback(InputEvent* input, void* _ctx) {
    BtScannerState* state = (BtScannerState*)_ctx;

    if(furi_mutex_acquire(state->mutex, 100) != FuriStatusOk) return;

    if(state->show_details) {
        if(input->type == InputTypeShort) {
            switch(input->key) {
            case InputKeyBack:
                state->show_details = false;
                break;
            case InputKeyOk:
                save_log(state);
                state->show_details = false;
                break;
            default:
                break;
            }
        }
    } else {
        if(input->type == InputTypeShort || input->type == InputTypeRepeat) {
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
                if(state->scanning) {
                    stop_scanning(state);
                } else if(state->device_count > 0) {
                    state->show_details = true;
                } else {
                    start_scanning(state);
                }
                break;
            case InputKeyBack:
                if(state->scanning) {
                    stop_scanning(state);
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
static BtScannerState* bt_scanner_alloc() {
    BtScannerState* state = malloc(sizeof(BtScannerState));
    memset(state, 0, sizeof(BtScannerState));
    
    state->ctx.gui = furi_record_open(RECORD_GUI);
    state->ctx.notification = furi_record_open(RECORD_NOTIFICATION);
    state->ctx.storage = furi_record_open(RECORD_STORAGE);
    state->ctx.dialogs = furi_record_open(RECORD_DIALOGS);
    
    state->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    
    state->ctx.view_port = view_port_alloc();
    view_port_draw_callback_set(state->ctx.view_port, draw_callback, state);
    view_port_input_callback_set(state->ctx.view_port, input_callback, state);
    gui_add_view_port(state->ctx.gui, state->ctx.view_port, GuiLayerFullscreen);
    
    return state;
}

// Очистка
static void bt_scanner_free(BtScannerState* state) {
    if(!state) return;
    
    if(state->scanning) {
        stop_scanning(state);
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

// Главная функция
int32_t bt_scanner_app(void* p) {
    UNUSED(p);
    
    BtScannerState* state = bt_scanner_alloc();
    FURI_LOG_I(TAG, "REAL BT Scanner started");
    
    bool running = true;
    while(running) {
        view_port_update(state->ctx.view_port);
        
        // Автостоп сканирования
        if(state->scanning) {
            uint32_t elapsed = furi_get_tick() - state->scan_start_time;
            if(elapsed >= SCAN_DURATION_MS) {
                stop_scanning(state);
            }
        }
        
        furi_delay_ms(50);
    }
    
    bt_scanner_free(state);
    return 0;
}
