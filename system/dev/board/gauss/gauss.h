// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <ddk/protocol/clk.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/iommu.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/usb-mode-switch.h>
#include <soc/aml-a113/a113-clocks.h>

#include <threads.h>

enum {
    AML_I2C_A,
    AML_I2C_B,
    AML_I2C_C,
    AML_I2C_D,
};

// BTI IDs for our devices
enum {
    BTI_BOARD,
    BTI_AUDIO_IN,
    BTI_AUDIO_OUT,
    BTI_USB_XHCI,
};

typedef struct {
    platform_bus_protocol_t pbus;
    zx_device_t* parent;
    gpio_protocol_t gpio;
    i2c_protocol_t i2c;
    usb_mode_switch_protocol_t usb_mode_switch;
    clk_protocol_t clk;
    iommu_protocol_t iommu;
    zx_handle_t bti_handle;
    io_buffer_t usb_phy;
    zx_handle_t usb_phy_irq_handle;
    thrd_t phy_irq_thread;
    a113_clk_dev_t *clocks;
} gauss_bus_t;

// gauss-audio.c
zx_status_t gauss_audio_init(gauss_bus_t* bus);

// gauss-gpio.c
zx_status_t gauss_gpio_init(gauss_bus_t* bus);

// gauss-i2c.c
zx_status_t gauss_i2c_init(gauss_bus_t* bus);

// gauss-usb.c
zx_status_t gauss_usb_init(gauss_bus_t* bus);
zx_status_t gauss_usb_set_mode(gauss_bus_t* bus, usb_mode_t mode);

// gauss-clk.c
zx_status_t gauss_clk_init(gauss_bus_t* bus);

// gauss-pcie.c
zx_status_t gauss_pcie_init(gauss_bus_t* bus);
