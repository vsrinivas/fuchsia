// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-device.h>
#include <mdi/mdi.h>
#include <mdi/mdi-defs.h>
#include <magenta/types.h>

// represents an MMIO resource
typedef struct {
    mx_paddr_t base;
    size_t length;
    mx_handle_t resource;
} platform_mmio_t;

// represents an IRQ resource
typedef struct {
    uint32_t irq;
    mx_handle_t resource;
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
    mx_device_t* mxdev;
    pbus_interface_t interface;
    mx_handle_t resource;   // root resource for platform bus
    mdi_node_ref_t  platform_node;
    mdi_node_ref_t  bus_node;
    uintptr_t mdi_addr;
    size_t mdi_size;
    mx_handle_t mdi_handle;
    uint32_t vid;
    uint32_t pid;
    // resources must be last
    platform_resources_t resources;
} platform_bus_t;

// context structure for a platform device
typedef struct {
    mx_device_t* mxdev;
    platform_bus_t* bus;
    // resources must be last
    platform_resources_t resources;
} platform_dev_t;

// platform-device.c
mx_status_t platform_bus_publish_device(platform_bus_t* bus, mdi_node_ref_t* device_node);

// platform-resources.c
void platform_release_resources(platform_resources_t* resources);
mx_status_t platform_map_mmio(platform_resources_t* resources, uint32_t index,
                              uint32_t cache_policy, void** vaddr, size_t* size,
                                     mx_handle_t* out_handle);
mx_status_t platform_map_interrupt(platform_resources_t* resources, uint32_t index,
                                   mx_handle_t* out_handle);
void platform_init_resources(platform_resources_t* resources, uint32_t mmio_count,
                             uint32_t irq_count);
mx_status_t platform_add_resources(platform_bus_t* bus, platform_resources_t* resources,
                                   mdi_node_ref_t* node);
