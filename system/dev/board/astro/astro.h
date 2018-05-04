// Copyright 2018 The Fuchsia Authors. All rights reserved.
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
};

typedef struct {
    platform_bus_protocol_t pbus;
    gpio_protocol_t gpio;
    i2c_protocol_t i2c;
    serial_impl_protocol_t serial;
    zx_device_t* parent;
    iommu_protocol_t iommu;
} aml_bus_t;

// aml-gpio.c
zx_status_t aml_gpio_init(aml_bus_t* bus);

// aml-i2c.c
zx_status_t aml_i2c_init(aml_bus_t* bus);

// aml-usb.c
zx_status_t aml_usb_init(aml_bus_t* bus);
