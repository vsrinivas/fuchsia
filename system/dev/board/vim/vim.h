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
#include <ddk/protocol/serial.h>

// BTI IDs for our devices
enum {
    BTI_BOARD,
    BTI_USB_XHCI,
    BTI_MALI,
    BTI_DISPLAY,
};

typedef struct {
    platform_bus_protocol_t pbus;
    gpio_protocol_t gpio;
    i2c_protocol_t i2c;
    serial_impl_protocol_t serial;
    zx_device_t* parent;
    iommu_protocol_t iommu;
    uint32_t soc_pid;
} vim_bus_t;

// vim-gpio.c
zx_status_t vim_gpio_init(vim_bus_t* bus);

// vim-i2c.c
zx_status_t vim_i2c_init(vim_bus_t* bus);

// vim-mali.c
zx_status_t vim_mali_init(vim_bus_t* bus);

// vim-uart.c
zx_status_t vim_uart_init(vim_bus_t* bus);

// vim-usb.c
zx_status_t vim_usb_init(vim_bus_t* bus);
