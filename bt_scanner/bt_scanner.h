#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_bt.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification.h>
#include <storage/storage.h>
#include <dialogs/dialogs.h>

#ifdef __cplusplus
extern "C" {
#endif

// Главная функция приложения - ДОЛЖНА СОВПАДАТЬ с entry_point в application.fam
int32_t bt_scanner_app(void* p);

#ifdef __cplusplus
}
#endif
