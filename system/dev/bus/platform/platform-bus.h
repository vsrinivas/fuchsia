// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-device.h>
#include <zircon/types.h>

// represents an MMIO resource
typedef struct {
    zx_paddr_t base;
    size_t length;
} platform_mmio_t;

// represents an IRQ resource
typedef struct {
    uint32_t irq;
} platform_irq_t;

// collection of resources, embedded in both the platform_bus_t and platform_dev_t structs
typedef struct {
    platform_mmio_t* mmios;
    platform_irq_t* irqs;
    uint32_t mmio_count;
    uint32_t irq_count;
    uint8_t extra[];        // extra storage for mmios and irqs
} platform_resources_t;

// context structure for the platform bus
typedef struct {
    zx_device_t* zxdev;
    pbus_interface_t interface;
    zx_handle_t resource;   // root resource for platform bus
    uint32_t vid;
    uint32_t pid;

    list_node_t devices;    // list of platform_dev_t
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

    // resources must be last
    platform_resources_t resources;
} platform_dev_t;

// platform-device.c
void platform_dev_free(platform_dev_t* dev);
zx_status_t platform_device_add(platform_bus_t* bus, const pbus_dev_t* dev, uint32_t flags);
zx_status_t platform_device_enable(platform_dev_t* dev, bool enable);

// platform-resources.c
zx_status_t platform_map_mmio(platform_dev_t* dev, uint32_t index, uint32_t cache_policy,
                              void** vaddr, size_t* size, zx_handle_t* out_handle);
zx_status_t platform_map_interrupt(platform_dev_t* dev, uint32_t index, zx_handle_t* out_handle);
void platform_init_resources(platform_resources_t* resources, uint32_t mmio_count,
                             uint32_t irq_count);
zx_status_t platform_bus_add_mmios(platform_bus_t* bus, platform_resources_t* resources,
                                   const pbus_mmio_t* mmios, size_t mmio_count);
zx_status_t platform_bus_add_irqs(platform_bus_t* bus, platform_resources_t* resources,
                                  const pbus_irq_t* irqs, size_t irq_count);
