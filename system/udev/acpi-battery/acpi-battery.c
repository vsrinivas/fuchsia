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
    mx_device_t device;

    acpi_handle_t acpi_handle;
    thrd_t poll_thread;

    mtx_t lock;

    uint32_t state;
    uint32_t capacity_full;
    uint32_t capacity_design;
    uint32_t capacity_remaining;
} acpi_battery_device_t;

#define get_acpi_battery_device(dev) containerof(dev, acpi_battery_device_t, device)

static ssize_t acpi_battery_read(mx_device_t* dev, void* buf, size_t count, mx_off_t off) {
    if (off > 0) {
        return 0;
    }
    acpi_battery_device_t* device = get_acpi_battery_device(dev);
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
    return rc;
}

static mx_protocol_device_t acpi_battery_device_proto = {
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

        mx_nanosleep(MX_MSEC(1000));
    }
    return 0;
}

static mx_status_t acpi_battery_bind(mx_driver_t* drv, mx_device_t* dev) {
    mx_acpi_protocol_t* acpi;
    if (device_get_protocol(dev, MX_PROTOCOL_ACPI, (void**)&acpi)) {
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

    device_init(&device->device, drv, "acpi-battery", &acpi_battery_device_proto);
    device_add(&device->device, dev);

    return NO_ERROR;
}

mx_driver_t _driver_acpi_battery = {
    .ops = {
        .bind = acpi_battery_bind,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_acpi_battery, "acpi-battery", "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_ACPI),
    // TODO(yky): match using hid when implemented
MAGENTA_DRIVER_END(_driver_acpi_battery)
