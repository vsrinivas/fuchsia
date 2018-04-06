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
enum
{
    BTI_BOARD,
    BTI_USB_XHCI,
};

typedef struct {
    platform_bus_protocol_t     pbus;
    zx_device_t*                parent;
    iommu_protocol_t            iommu;
    gpio_protocol_t             gpio;
    zx_handle_t                 bti_handle;
    imx8m_t*                    imx8m;
    uint32_t                    soc_pid;

} imx8mevk_bus_t;


zx_status_t imx8m_gpio_init(imx8mevk_bus_t* bus);
zx_status_t imx_usb_init(imx8mevk_bus_t* bus);

