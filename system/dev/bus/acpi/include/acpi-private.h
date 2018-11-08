// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef ZIRCON_SYSTEM_DEV_BUS_ACPI_INCLUDE_ACPI_PRIVATE_H_
#define ZIRCON_SYSTEM_DEV_BUS_ACPI_INCLUDE_ACPI_PRIVATE_H_
#include <ddk/device.h>
#include <ddk/protocol/auxdata.h>
#include <ddk/protocol/pciroot.h>

#define MAX_NAMESPACE_DEPTH 100

typedef struct acpi_device_resource {
    bool writeable;
    uint32_t base_address;
    uint32_t alignment;
    uint32_t address_length;
} acpi_device_resource_t;

typedef struct acpi_device_irq {
    uint8_t trigger;
#define ACPI_IRQ_TRIGGER_LEVEL 0
#define ACPI_IRQ_TRIGGER_EDGE 1
    uint8_t polarity;
#define ACPI_IRQ_ACTIVE_HIGH 0
#define ACPI_IRQ_ACTIVE_LOW 1
#define ACPI_IRQ_ACTIVE_BOTH 2
    uint8_t sharable;
#define ACPI_IRQ_EXCLUSIVE 0
#define ACPI_IRQ_SHARED 1
    uint8_t wake_capable;
    uint8_t pin;
} acpi_device_irq_t;

typedef struct acpi_device {
    zx_device_t* zxdev;

    mtx_t lock;

    bool got_resources;

    // memory resources from _CRS
    acpi_device_resource_t* resources;
    size_t resource_count;

    // interrupt resources from _CRS
    acpi_device_irq_t* irqs;
    size_t irq_count;

    // handle to the corresponding ACPI node
    ACPI_HANDLE ns_node;
} acpi_device_t;

typedef struct {
    zx_device_t* parent;
    bool found_pci;
    uint8_t last_pci; // bus number of the last PCI root seen
} publish_acpi_device_ctx_t;

typedef struct {
    uint8_t max;
    uint8_t i;
    auxdata_i2c_device_t* data;
} pci_child_auxdata_ctx_t;

// TODO(cja): this is here because of kpci.c and can be removed once
// kernel pci is out of the tree.
zx_device_t* publish_device(zx_device_t* parent,
                            ACPI_HANDLE handle,
                            ACPI_DEVICE_INFO* info,
                            const char* name,
                            uint32_t protocol_id,
                            void* protocol_ops);

zx_protocol_device_t* get_acpi_root_device_proto(void);

#endif // ZIRCON_SYSTEM_DEV_BUS_ACPI_INCLUDE_ACPI_PRIVATE_H_
