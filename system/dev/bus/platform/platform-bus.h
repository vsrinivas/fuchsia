// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/usb-mode-switch.h>
#include <zircon/types.h>

// context structure for the platform bus
typedef struct {
    zx_device_t* zxdev;
    pbus_interface_t interface;
    usb_mode_switch_protocol_t ums;
    zx_handle_t resource;   // root resource for platform bus
    uint32_t vid;
    uint32_t pid;

    list_node_t devices;    // list of platform_dev_t
    char board_name[ZX_DEVICE_NAME_MAX + 1];
} platform_bus_t;

// context structure for a platform device
typedef struct {
    zx_device_t* zxdev;
    platform_bus_t* bus;
    list_node_t node;
    char name[ZX_DEVICE_NAME_MAX + 1];
    uint32_t vid;
    uint32_t pid;
    uint32_t did;
    bool enabled;

    pbus_mmio_t* mmios;
    pbus_irq_t* irqs;
    uint32_t mmio_count;
    uint32_t irq_count;
} platform_dev_t;

// platform-device.c
void platform_dev_free(platform_dev_t* dev);
zx_status_t platform_device_add(platform_bus_t* bus, const pbus_dev_t* dev, uint32_t flags);
zx_status_t platform_device_enable(platform_dev_t* dev, bool enable);
