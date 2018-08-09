// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/iommu.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/serial-impl.h>

// BTI IDs for our devices
enum {
    BTI_BOARD,
    BTI_USB_XHCI,
    BTI_MALI,
    BTI_DISPLAY,
    BTI_VIDEO,
    BTI_AUDIO,
    BTI_EMMC,
    BTI_SDIO,
    BTI_CANVAS,
};

typedef struct {
    platform_bus_protocol_t pbus;
    gpio_protocol_t gpio;
    i2c_protocol_t i2c;
    serial_impl_protocol_t serial;
    zx_device_t* parent;
    iommu_protocol_t iommu;
} vim_bus_t;

// vim-gpio.c
zx_status_t vim_gpio_init(vim_bus_t* bus);

// vim-i2c.c
zx_status_t vim_i2c_init(vim_bus_t* bus);

// vim-mali.c
zx_status_t vim_mali_init(vim_bus_t* bus, uint32_t bti_index);

// vim-uart.c
zx_status_t vim_uart_init(vim_bus_t* bus);

// vim-usb.c
zx_status_t vim_usb_init(vim_bus_t* bus);

// vim-sd-emmc.c
zx_status_t vim_sd_emmc_init(vim_bus_t* bus);

// vim-sd-emmc.c
zx_status_t vim_sdio_init(vim_bus_t* bus);

// vim-eth.c
zx_status_t vim_eth_init(vim_bus_t* bus);

// vim-fanctl.c
zx_status_t vim2_thermal_init(vim_bus_t* bus);

// vim-mailbox.c
zx_status_t vim2_mailbox_init(vim_bus_t* bus);

// vim-display.c
zx_status_t vim_display_init(vim_bus_t* bus);

// vim-video.c
zx_status_t vim_video_init(vim_bus_t* bus);

// vim-led2472g.c
zx_status_t vim_led2472g_init(vim_bus_t* bus);

// vim-rtc.c
zx_status_t vim_rtc_init(vim_bus_t* bus);

// vim-canvas.c
zx_status_t vim2_canvas_init(vim_bus_t* bus);
