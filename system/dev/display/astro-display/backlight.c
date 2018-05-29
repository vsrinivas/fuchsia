// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "astro-display.h"

// Table from Linux source
// TODO: Need to separate backlight driver from display driver (ZX-2455)
typedef struct {
    uint8_t reg;
    uint8_t val;
} bl_i2c_cmd_t;

static const bl_i2c_cmd_t backlight_init_table[] = {
    {0xa2, 0x20},
    {0xa5, 0x54},
    {0x00, 0xff},
    {0x01, 0x05},
    {0xa2, 0x20},
    {0xa5, 0x54},
    {0xa1, 0xb7},
    {0xa0, 0xff},
    {0x00, 0x80},
};

void init_backlight(astro_display_t* display) {

    // power on backlight
    gpio_config(&display->gpio, GPIO_BL, GPIO_DIR_OUT);
    gpio_write(&display->gpio, GPIO_BL, 1);
    usleep(1000);

    for (size_t i = 0; i < countof(backlight_init_table); i++) {
        if (i2c_transact_sync(&display->i2c, 0, &backlight_init_table[i], 2, NULL, 0) != ZX_OK) {
            DISP_ERROR("Backlight write failed: reg[0x%x]: 0x%x\n", backlight_init_table[i].reg,
                       backlight_init_table[i].val);
        }
    }
}
