// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <magenta/compiler.h>
#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>

#include "init.h"
#include "ec.h"
#include "pci.h"
#include "powerbtn.h"
#include "processor.h"
#include "resource_tree.h"
#include "power.h"

#define TRACE 0

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

mx_handle_t root_resource_handle;
mx_handle_t rpc_handle;

static int acpi_rpc_thread(void* arg) {
    xprintf("bus-acpi: rpc thread starting\n");
    mx_status_t status = begin_processing(rpc_handle);
    xprintf("bus-acpi: rpc thread returned %d\n", status);
    return (status == MX_OK) ? 0 : -1;
}

static mx_protocol_device_t acpi_device_proto = {
    .version = DEVICE_OPS_VERSION,
};

static mx_status_t acpi_add_pci_root_device(mx_device_t* parent, const char* name) {
    mx_device_t* dev;
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ctx = NULL,
        .ops = &acpi_device_proto,
        .proto_id = MX_PROTOCOL_ACPI,
    };

    mx_status_t status = device_add(parent, &args, &dev);
    if (status != MX_OK) {
        xprintf("acpi-bus: error %d in device_add\n", status);
    }
    return status;
}

static mx_status_t acpi_drv_bind(void* ctx, mx_device_t* parent, void** cookie) {
    // ACPI is the root driver for its devhost so run init in the bind thread.
    xprintf("bus-acpi: bind to %s %p\n", device_get_name(parent), parent);
    root_resource_handle = get_root_resource();

    // Get RPC channel
    rpc_handle = mx_get_startup_handle(PA_HND(PA_USER0, 10));
    if (rpc_handle == MX_HANDLE_INVALID) {
        xprintf("bus-acpi: no acpi rpc handle\n");
        return MX_ERR_INVALID_ARGS;
    }

    if (init() != MX_OK) {
        xprintf("bus_acpi: failed to initialize ACPI\n");
        return MX_ERR_INTERNAL;
    }

    printf("acpi-bus: initialized\n");

    ec_init();

    mx_status_t status = install_powerbtn_handlers();
    if (status != MX_OK) {
        xprintf("acpi-bus: error %d in install_powerbtn_handlers\n", status);
        return status;
    }

    // Report current resources to kernel PCI driver
    status = pci_report_current_resources(get_root_resource());
    if (status != MX_OK) {
        xprintf("acpi-bus: WARNING: ACPI failed to report all current resources!\n");
    }

    // Initialize kernel PCI driver
    mx_pci_init_arg_t* arg;
    uint32_t arg_size;
    status = get_pci_init_arg(&arg, &arg_size);
    if (status != MX_OK) {
        xprintf("acpi-bus: erorr %d in get_pci_init_arg\n", status);
        return status;
    }

    status = mx_pci_init(get_root_resource(), arg, arg_size);
    if (status != MX_OK) {
        xprintf("acpi-bus: error %d in mx_pci_init\n", status);
        return status;
    }

    free(arg);

    // start rpc thread
    thrd_t rpc_thrd;
    int rc = thrd_create_with_name(&rpc_thrd, acpi_rpc_thread, NULL, "acpi-rpc");
    if (rc != thrd_success) {
        xprintf("acpi-bus: error %d in rpc thrd_create\n", rc);
        return MX_ERR_INTERNAL;
    }

    // Publish PCI root device
    // TODO: publish other ACPI devices
    return acpi_add_pci_root_device(parent, "pci-root");
}

#if 0
// Make this a bus driver when more ACPI devices other than PCI root are published
static mx_status_t acpi_drv_create(void* ctx, mx_device_t* parent,
                                   const char* name, const char* args, mx_handle_t resource) {
    xprintf("acpi_drv_create: name=%s\n", name);
    return acpi_add_pci_root_device(parent, name);
}
#endif

static mx_driver_ops_t acpi_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = acpi_drv_bind,
};

MAGENTA_DRIVER_BEGIN(acpi, acpi_driver_ops, "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_ACPI_BUS),
MAGENTA_DRIVER_END(acpi)
