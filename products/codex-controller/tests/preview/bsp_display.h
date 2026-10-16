#pragma once
#include <stdbool.h>
#include "esp_err.h"
bool bsp_lvgl_lock(int timeout_ms);
void bsp_lvgl_unlock(void);
