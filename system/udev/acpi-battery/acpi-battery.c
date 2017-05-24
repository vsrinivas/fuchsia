// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/acpi.h>

#include <acpisvc/simple.h>
#include <magenta/types.h>
#include <magenta/syscalls.h>
#include <stdlib.h>
#include <stdio.h>
#include <threads.h>

#define TRACE 1

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

#define ACPI_BATTERY_STATE_DISCHARGING (1 << 0)
#define ACPI_BATTERY_STATE_CHARGING    (1 << 1)
#define ACPI_BATTERY_STATE_CRITICAL    (1 << 2)

typedef struct acpi_battery_device {
    mx_device_t* mxdev;

    acpi_handle_t acpi_handle;
    thrd_t poll_thread;

    mtx_t lock;

    uint32_t state;
    uint32_t capacity_full;
    uint32_t capacity_design;
    uint32_t capacity_remaining;
} acpi_battery_device_t;

static mx_status_t acpi_battery_read(void* ctx, void* buf, size_t count, mx_off_t off, size_t* actual) {
    acpi_battery_device_t* device = ctx;
    mtx_lock(&device->lock);
    ssize_t rc = 0;
    int pct;
    if ((device->capacity_remaining == 0xffffffff) || ((device->capacity_full == 0xffffffff) && device->capacity_design == 0xffffffff) || device->capacity_full == 0) {
        pct = -1;
    } else {
        pct = device->capacity_remaining * 100 / device->capacity_full;
    }
    if (pct == -1) {
        rc = snprintf(buf, count, "error");
    } else {
        rc = snprintf(buf, count, "%s%d%%", (device->state & ACPI_BATTERY_STATE_CHARGING) ? "c" : "", pct);
    }
    if (rc > 0 && (size_t)rc < count) {
        rc += 1; // null terminator
    }
    mtx_unlock(&device->lock);
    if (rc < 0) {
        return rc;
    }
    *actual = rc;
    return NO_ERROR;
}

static mx_protocol_device_t acpi_battery_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .read = acpi_battery_read,
    // TODO(yky): release
};

static int acpi_battery_poll_thread(void* arg) {
    acpi_battery_device_t* dev = arg;
    for (;;) {
        acpi_rsp_bst_t* bst;
        mx_status_t status = acpi_bst(&dev->acpi_handle, &bst);
        if (status != NO_ERROR) {
            continue;
        }

        acpi_rsp_bif_t* bif;
        status = acpi_bif(&dev->acpi_handle, &bif);
        if (status != NO_ERROR) {
            free(bst);
            continue;
        }

        mtx_lock(&dev->lock);
        dev->state = bst->state;
        dev->capacity_remaining = bst->capacity_remaining;
        dev->capacity_full = bif->capacity_full;
        dev->capacity_design = bif->capacity_design;
        mtx_unlock(&dev->lock);
        free(bif);
        free(bst);

        mx_nanosleep(mx_deadline_after(MX_MSEC(1000)));
    }
    return 0;
}

static mx_status_t acpi_battery_bind(void* ctx, mx_device_t* dev, void** cookie) {
    mx_acpi_protocol_t* acpi;
    if (device_op_get_protocol(dev, MX_PROTOCOL_ACPI, (void**)&acpi)) {
        return ERR_NOT_SUPPORTED;
    }

    mx_handle_t handle = acpi->clone_handle(dev);
    if (handle <= 0) {
        printf("acpi-battery: error cloning handle (%d)\n", handle);
        return handle;
    }

    acpi_battery_device_t* device = calloc(1, sizeof(acpi_battery_device_t));
    if (!device) {
        mx_handle_close(handle);
        return ERR_NO_MEMORY;
    }
    acpi_handle_init(&device->acpi_handle, handle);

    int rc = thrd_create_with_name(&device->poll_thread, acpi_battery_poll_thread, device, "acpi-battery-poll");
    if (rc != thrd_success) {
        printf("acpi-battery: polling thread did not start (%d)\n", rc);
    }

   device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "acpi-battery",
        .ctx = device,
        .ops = &acpi_battery_device_proto,
        .proto_id = MX_PROTOCOL_BATTERY,
    };

    mx_status_t status = device_add(dev, &args, &device->mxdev);
    if (status != NO_ERROR) {
        printf("acpi-battery: could not add device! err=%d\n", status);
        free(device);
        return status;
    }

    return NO_ERROR;
}

static mx_driver_ops_t acpi_battery_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = acpi_battery_bind,
};

#define ACPI_BATTERY_HID_0_3 0x504e5030 // "PNP0"
#define ACPI_BATTERY_HID_4_7 0x43304100 // "C0A"

MAGENTA_DRIVER_BEGIN(acpi_battery, acpi_battery_driver_ops, "magenta", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_ACPI),
    BI_ABORT_IF(NE, BIND_ACPI_HID_0_3, ACPI_BATTERY_HID_0_3),
    BI_MATCH_IF(EQ, BIND_ACPI_HID_4_7, ACPI_BATTERY_HID_4_7),
MAGENTA_DRIVER_END(acpi_battery)
