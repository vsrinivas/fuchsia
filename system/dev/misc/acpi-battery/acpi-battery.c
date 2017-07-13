// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/acpi.h>

#include <magenta/types.h>
#include <magenta/syscalls.h>
#include <stdlib.h>
#include <stdio.h>
#include <threads.h>

#define TRACE 0

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

    // acpi protocol
    acpi_protocol_t acpi;

    // thread to poll for battery status
    thrd_t poll_thread;

    mtx_t lock;

    // current battery status
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
    return MX_OK;
}

static void acpi_battery_release(void* ctx) {
    free(ctx);
}

static mx_protocol_device_t acpi_battery_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .read = acpi_battery_read,
    .release = acpi_battery_release,
};

static int acpi_battery_poll_thread(void* arg) {
    acpi_battery_device_t* dev = arg;
    for (;;) {
        acpi_rsp_bst_t bst;
        if (acpi_BST(&dev->acpi, &bst) != MX_OK) {
            break;
        }

        acpi_rsp_bif_t bif;
        if (acpi_BIF(&dev->acpi, &bif) != MX_OK) {
            break;
        }

        mtx_lock(&dev->lock);
        dev->state = bst.battery_state;
        dev->capacity_remaining = bst.battery_remaining_capacity;

        dev->capacity_design = bif.design_capacity;
        dev->capacity_full = bif.last_full_charge_capacity;
        mtx_unlock(&dev->lock);

        mx_nanosleep(mx_deadline_after(MX_MSEC(1000)));
    }
    printf("acpi-battery: poll thread exiting\n");
    return 0;
}

static mx_status_t acpi_battery_bind(void* ctx, mx_device_t* parent, void** cookie) {
    xprintf("acpi-battery: bind\n");

    acpi_battery_device_t* dev = calloc(1, sizeof(acpi_battery_device_t));
    if (!dev) {
        return MX_ERR_NO_MEMORY;
    }

    if (device_get_protocol(parent, MX_PROTOCOL_ACPI, &dev->acpi)) {
        free(dev);
        return MX_ERR_NOT_SUPPORTED;
    }

    int rc = thrd_create_with_name(&dev->poll_thread, acpi_battery_poll_thread, dev, "acpi-battery-poll");
    if (rc != thrd_success) {
        xprintf("acpi-battery: polling thread did not start (%d)\n", rc);
        free(dev);
        return rc;
    }

   device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "acpi-battery",
        .ctx = dev,
        .ops = &acpi_battery_device_proto,
        .proto_id = MX_PROTOCOL_BATTERY,
    };

    mx_status_t status = device_add(parent, &args, NULL);
    if (status != MX_OK) {
        xprintf("acpi-battery: could not add device! err=%d\n", status);
        free(dev);
        return status;
    }

    printf("acpi-battery: initialized\n");

    return MX_OK;
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
