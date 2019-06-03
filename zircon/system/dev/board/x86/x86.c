// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/driver.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/status.h>
#include <zircon/threads.h>
#include "acpi.h"
#include "smbios.h"
#include "sysmem.h"

zx_handle_t root_resource_handle;

typedef struct {
    pbus_protocol_t pbus;
    zx_device_t* parent;
    zx_device_t* sys_root;
    zx_device_t* acpi_root;
} pbus_x86_t;

static void x86_root_release(void* ctx) {
    pbus_x86_t* x86 = ctx;

    free(x86);
}

static zx_protocol_device_t acpi_root_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = x86_root_release,
};

static zx_status_t sys_device_suspend(void* ctx, uint32_t flags) {
    return acpi_suspend(flags);
}

static int x86_start_thread(void* arg) {
    pbus_x86_t* x86 = arg;
    zx_status_t status = publish_sysmem(&x86->pbus);
    if (status != ZX_OK) {
        zxlogf(ERROR, "publish_sysmem failed: %d\n", status);
        goto fail;
    }
    return publish_acpi_devices(x86->parent, x86->sys_root, x86->acpi_root);

fail:
    return status;
}

static zx_status_t x86_bind(void* ctx, zx_device_t* parent) {
    // Please do not use get_root_resource() in new code. See ZX-1467.
    root_resource_handle = get_root_resource();

    pbus_x86_t* x86 = calloc(1, sizeof(pbus_x86_t));
    if (!x86) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PBUS, &x86->pbus);
    if (status != ZX_OK) {
        free(x86);
        return ZX_ERR_NOT_SUPPORTED;
    }

    // Do ACPI init.
    status = acpi_init();
    if (status != ZX_OK) {
        free(x86);
        zxlogf(ERROR, "%s: failed to initialize ACPI %d \n", __func__, status);
        return ZX_ERR_INTERNAL;
    }

    zx_device_t* sys_root = device_get_parent(parent);
    if (sys_root == NULL) {
        free(x86);
        zxlogf(ERROR, "%s: failed to find parent node of platform (expected sys)\n", __func__);
        return ZX_ERR_INTERNAL;
    }

    // publish acpi root
    device_add_args_t args2 = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "acpi",
        .ctx = x86,
        .ops = &acpi_root_device_proto,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    zx_device_t* acpi_root = NULL;
    // We create the acpi root under /dev/sys/platform, and pci gets
    // created under /dev/sys (to preserve compatibility).
    status = device_add(parent, &args2, &acpi_root);
    if (status != ZX_OK) {
        free(x86);
        zxlogf(ERROR, "acpi: error %d in device_add(sys/platform/acpi)\n", status);
        return status;
    }

    char board_name[fuchsia_sysinfo_SYSINFO_BOARD_NAME_LEN + 1];
    size_t board_name_actual = 0;
    status = smbios_get_board_name(board_name, sizeof(board_name), &board_name_actual);
    if (status != ZX_OK) {
        if (status == ZX_ERR_BUFFER_TOO_SMALL) {
            zxlogf(INFO, "acpi: smbios board name too big for sysinfo\n");
        } else if (status != ZX_ERR_NOT_FOUND) {
            zxlogf(ERROR, "acpi: smbios board name could not be read: %s\n",
                   zx_status_get_string(status));
        }
        strcpy(board_name, "pc");
        board_name_actual = strlen(board_name) + 1;
    }

    // Publish board name to sysinfo driver
    status = device_publish_metadata(acpi_root, "/dev/misc/sysinfo", DEVICE_METADATA_BOARD_NAME,
                                     board_name, board_name_actual);
    if (status != ZX_OK) {
        zxlogf(ERROR, "device_publish_metadata(board_name) failed: %d\n", status);
    }

    x86->sys_root = sys_root;
    x86->acpi_root = acpi_root;
    x86->parent = parent;
    thrd_t t;
    int thrd_rc = thrd_create_with_name(&t, x86_start_thread, x86, "x86_start_thread");
    if (thrd_rc != thrd_success) {
        status = thrd_status_to_zx_status(thrd_rc);
        zxlogf(ERROR, "%s: Failed to create start thread: %d\n", __func__, status);
        goto fail;
    }

    // Set the "sys" suspend op in platform-bus.
    // The devmgr coordinator code that arranges ordering in which the suspend hooks
    // are called makes sure the suspend hook attached to sys/ is called dead last,
    // (coordinator.cpp:BuildSuspendList()). If move this suspend hook elsewhere,
    // we must make sure that the coordinator code arranges for this suspend op to be
    // called last.
    pbus_sys_suspend_t suspend = {sys_device_suspend, NULL};
    status = pbus_register_sys_suspend_callback(&x86->pbus, &suspend);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Could not register suspend callback: %d\n", __func__, status);
    }

    return ZX_OK;
fail:
    return status;
}

static zx_driver_ops_t x86_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = x86_bind,
};

ZIRCON_DRIVER_BEGIN(acpi_bus, x86_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PBUS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_INTEL),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_X86),
ZIRCON_DRIVER_END(acpi_bus)
