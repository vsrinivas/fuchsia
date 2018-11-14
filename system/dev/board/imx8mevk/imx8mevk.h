// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/protocol/gpioimpl.h>
#include <ddk/protocol/iommu.h>
#include <ddk/protocol/platform/bus.h>

// BTI IDs for our devices
enum {
    BTI_BOARD,
    BTI_USB1,
    BTI_USB2,
    BTI_DISPLAY,
    BTI_GPU,
    BTI_SDHCI,

};

typedef struct {
    pbus_protocol_t pbus;
    zx_device_t* parent;
    iommu_protocol_t iommu;
    gpio_impl_protocol_t gpio;
    zx_handle_t bti_handle;
    uint32_t soc_pid;
} imx8mevk_bus_t;

zx_status_t imx8m_gpio_init(imx8mevk_bus_t* bus);
zx_status_t imx_usb_init(imx8mevk_bus_t* bus);
zx_status_t imx_i2c_init(imx8mevk_bus_t* bus);
zx_status_t imx_gpu_init(imx8mevk_bus_t* bus);
zx_status_t imx8m_sdhci_init(imx8mevk_bus_t* bus);
