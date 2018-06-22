// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <threads.h>
#include <ddk/device.h>
#include <ddk/protocol/clk.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/canvas.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/iommu.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/usb-mode-switch.h>
#include <ddk/protocol/mailbox.h>
#include <ddk/protocol/scpi.h>
#include <sync/completion.h>
#include <zircon/boot/image.h>
#include <zircon/types.h>

typedef struct pdev_req pdev_req_t;

// this struct is local to platform-i2c.c
typedef struct platform_i2c_bus platform_i2c_bus_t;

// context structure for the platform bus
typedef struct {
    zx_device_t* zxdev;
    usb_mode_switch_protocol_t ums;
    gpio_protocol_t gpio;
    mailbox_protocol_t mailbox;
    scpi_protocol_t scpi;
    i2c_impl_protocol_t i2c;
    clk_protocol_t clk;
    iommu_protocol_t iommu;
    canvas_protocol_t canvas;
    zx_handle_t resource;   // root resource for platform bus
    zbi_platform_id_t platform_id;
    uint8_t* metadata;   // metadata extracted from ZBI
    size_t metadata_size;

    list_node_t devices;    // list of platform_dev_t

    platform_i2c_bus_t* i2c_buses;
    uint32_t i2c_bus_count;

    zx_handle_t dummy_iommu_handle;

    completion_t proto_completion;
} platform_bus_t;

// context structure for a platform device
typedef struct {
    zx_device_t* zxdev;
    platform_bus_t* bus;
    list_node_t node;
    char name[ZX_DEVICE_NAME_MAX + 1];
    uint32_t flags;
    uint32_t vid;
    uint32_t pid;
    uint32_t did;
    serial_port_info_t serial_port_info;
    bool enabled;

    pbus_mmio_t* mmios;
    pbus_irq_t* irqs;
    pbus_gpio_t* gpios;
    pbus_i2c_channel_t* i2c_channels;
    pbus_clk_t* clks;
    pbus_bti_t* btis;
    pbus_boot_metadata_t* boot_metadata;
    uint32_t mmio_count;
    uint32_t irq_count;
    uint32_t gpio_count;
    uint32_t i2c_channel_count;
    uint32_t clk_count;
    uint32_t bti_count;
    uint32_t boot_metadata_count;
} platform_dev_t;

// platform-bus.c
zx_status_t platform_bus_get_protocol(void* ctx, uint32_t proto_id, void* protocol);

// platform-device.c
void platform_dev_free(platform_dev_t* dev);
zx_status_t platform_device_add(platform_bus_t* bus, const pbus_dev_t* dev, uint32_t flags);
zx_status_t platform_device_enable(platform_dev_t* dev, bool enable);

// platform-i2c.c
zx_status_t platform_i2c_init(platform_bus_t* bus, i2c_impl_protocol_t* i2c);
zx_status_t platform_i2c_transact(platform_bus_t* bus, pdev_req_t* req, pbus_i2c_channel_t* channel,
                                  const void* write_buf, zx_handle_t channel_handle);
