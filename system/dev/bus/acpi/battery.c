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

#include <acpica/acpi.h>

#include "battery.h"

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

    ACPI_HANDLE acpi_handle;

    ACPI_BUFFER bst_buffer;
    ACPI_BUFFER bif_buffer;

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
    acpi_battery_device_t* dev = ctx;
    if (dev->bst_buffer.Length != ACPI_ALLOCATE_BUFFER) {
        AcpiOsFree(dev->bst_buffer.Pointer);
    }
    if (dev->bif_buffer.Length != ACPI_ALLOCATE_BUFFER) {
        AcpiOsFree(dev->bif_buffer.Pointer);
    }
    free(dev);
}

static mx_protocol_device_t acpi_battery_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .read = acpi_battery_read,
    .release = acpi_battery_release,
};

static int acpi_battery_poll_thread(void* arg) {
    acpi_battery_device_t* dev = arg;
    for (;;) {
        ACPI_STATUS acpi_status = AcpiEvaluateObject(dev->acpi_handle,
                (char*)"_BST", NULL, &dev->bst_buffer);
        if (acpi_status != AE_OK) {
            xprintf("acpi-battery: acpi error 0x%x in _BST\n", acpi_status);
            goto out;
        }
        ACPI_OBJECT* bst_pkg = dev->bst_buffer.Pointer;
        if ((bst_pkg->Type != ACPI_TYPE_PACKAGE) || (bst_pkg->Package.Count != 4)) {
            xprintf("acpi-battery: unexpected _BST response\n");
            goto out;
        }
        ACPI_OBJECT* bst_elem = bst_pkg->Package.Elements;
        int i;
        for (i = 0; i < 4; i++) {
            if (bst_elem[i].Type != ACPI_TYPE_INTEGER) {
                xprintf("acpi-battery: unexpected _BST response\n");
                goto out;
            }
        }

        acpi_status = AcpiEvaluateObject(dev->acpi_handle,
                (char*)"_BIF", NULL, &dev->bif_buffer);
        if (acpi_status != AE_OK) {
            xprintf("acpi-battery: acpi error 0x%x in _BIF\n", acpi_status);
            goto out;
        }
        ACPI_OBJECT* bif_pkg = dev->bif_buffer.Pointer;
        if ((bif_pkg->Type != ACPI_TYPE_PACKAGE) || (bif_pkg->Package.Count != 13)) {
            xprintf("acpi-battery: unexpected _BIF response\n");
            goto out;
        }
        ACPI_OBJECT* bif_elem = bif_pkg->Package.Elements;
        for (i = 0; i < 9; i++) {
            if (bif_elem[i].Type != ACPI_TYPE_INTEGER) {
                xprintf("acpi-battery: unexpected _BIF response\n");
                goto out;
            }
        }
        for (i = 9; i < 13; i++) {
            if (bif_elem[i].Type != ACPI_TYPE_STRING) {
                xprintf("acpi-battery: unexpected _BIF response\n");
                goto out;
            }
        }

        mtx_lock(&dev->lock);
        dev->state = bst_elem[0].Integer.Value;
        dev->capacity_remaining = bst_elem[2].Integer.Value;

        dev->capacity_design = bif_elem[1].Integer.Value;
        dev->capacity_full = bif_elem[2].Integer.Value;
        mtx_unlock(&dev->lock);

        mx_nanosleep(mx_deadline_after(MX_MSEC(1000)));
    }
out:
    printf("acpi-battery: poll thread exiting\n");
    return 0;
}

mx_status_t battery_init(mx_device_t* parent, ACPI_HANDLE acpi_handle) {
    xprintf("acpi-battery: init\n");

    acpi_battery_device_t* dev = calloc(1, sizeof(acpi_battery_device_t));
    if (!dev) {
        return MX_ERR_NO_MEMORY;
    }

    dev->acpi_handle = acpi_handle;

    dev->bst_buffer.Length = ACPI_ALLOCATE_BUFFER;
    dev->bst_buffer.Pointer = NULL;

    dev->bif_buffer.Length = ACPI_ALLOCATE_BUFFER;
    dev->bif_buffer.Pointer = NULL;

    int rc = thrd_create_with_name(&dev->poll_thread,
            acpi_battery_poll_thread, dev, "acpi-battery-poll");
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
