// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/iommu.h>
#include <ddk/protocol/platform-bus.h>

// BTI IDs for our devices
enum {
    BTI_BOARD,
    BTI_USB_XHCI,
    BTI_DISPLAY,
    BTI_MALI,
    BTI_VIDEO,
    BTI_AML_RAW_NAND,
    BTI_SDIO,
    BTI_CANVAS,
};

typedef struct {
    zx_device_t* parent;
    platform_bus_protocol_t pbus;
    gpio_protocol_t gpio;
    iommu_protocol_t iommu;
} aml_bus_t;

// astro-gpio.c
zx_status_t aml_gpio_init(aml_bus_t* bus);

// astro-i2c.c
zx_status_t aml_i2c_init(aml_bus_t* bus);

// astro-bluetooth.c
zx_status_t aml_bluetooth_init(aml_bus_t* bus);

// astro-usb.c
zx_status_t aml_usb_init(aml_bus_t* bus);

// astro-display.c
zx_status_t aml_display_init(aml_bus_t* bus);

// These should match the mmio table defined in astro-i2c.c
enum {
    ASTRO_I2C_A0_0,
    ASTRO_I2C_2,
    ASTRO_I2C_3,
};

/* Astro I2C Devices */
#define I2C_BACKLIGHT_ADDR    (0x2C)
#define I2C_AMBIENTLIGHT_ADDR (0x39)
// astro-touch.c
zx_status_t astro_touch_init(aml_bus_t* bus);
// aml-raw_nand.c
zx_status_t aml_raw_nand_init(aml_bus_t* bus);
// astro-sdio.c
zx_status_t aml_sdio_init(aml_bus_t* bus);
// astro-canvas.c
zx_status_t aml_canvas_init(aml_bus_t* bus);
// astro-light.c
zx_status_t ams_light_init(aml_bus_t* bus);
// astro-thermal.c
zx_status_t aml_thermal_init(aml_bus_t* bus);
// astro-video.c
zx_status_t aml_video_init(aml_bus_t* bus);
