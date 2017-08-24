// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>

#include <magenta/types.h>
#include <magenta/syscalls.h>
#include <magenta/device/power.h>
#include <mxio/debug.h>
#include <stdlib.h>
#include <stdio.h>
#include <threads.h>

#include <acpica/acpi.h>

#include "dev.h"
#include "errors.h"

#define MXDEBUG 0

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

    // event to notify on
    mx_handle_t event;

    power_info_t power_info;
    battery_info_t battery_info;

    // deprecated
    uint32_t state;
    uint32_t capacity_full;
    uint32_t capacity_design;
    uint32_t capacity_remaining;
} acpi_battery_device_t;

static mx_status_t call_STA(acpi_battery_device_t* dev) {
    ACPI_OBJECT obj = {
        .Type = ACPI_TYPE_INTEGER,
    };
    ACPI_BUFFER buffer = {
        .Length = sizeof(obj),
        .Pointer = &obj,
    };
    ACPI_STATUS acpi_status = AcpiEvaluateObject(dev->acpi_handle, (char*)"_STA", NULL, &buffer);
    if (acpi_status != AE_OK) {
        return acpi_to_mx_status(acpi_status);
    }

    xprintf("acpi_battery: _STA returned 0x%llx\n", obj.Integer.Value);

    mtx_lock(&dev->lock);
    uint32_t old = dev->power_info.state;
    if (obj.Integer.Value & ACPI_STA_BATTERY_PRESENT) {
        dev->power_info.state |= POWER_STATE_ONLINE;
    } else {
        dev->power_info.state &= ~POWER_STATE_ONLINE;
    }

    if (old != dev->power_info.state) {
        mx_object_signal(dev->event, 0, MX_USER_SIGNAL_0);
    }
    mtx_unlock(&dev->lock);
    return MX_OK;
}

static mx_status_t call_BIF(acpi_battery_device_t* dev) {
    mtx_lock(&dev->lock);

    ACPI_STATUS acpi_status = AcpiEvaluateObject(dev->acpi_handle,
            (char*)"_BIF", NULL, &dev->bif_buffer);
    if (acpi_status != AE_OK) {
        xprintf("acpi-battery: acpi error 0x%x in _BIF\n", acpi_status);
        goto err;
    }
    ACPI_OBJECT* bif_pkg = dev->bif_buffer.Pointer;
    if ((bif_pkg->Type != ACPI_TYPE_PACKAGE) || (bif_pkg->Package.Count != 13)) {
        xprintf("acpi-battery: unexpected _BIF response\n");
        goto err;
    }
    ACPI_OBJECT* bif_elem = bif_pkg->Package.Elements;
    for (int i = 0; i < 9; i++) {
        if (bif_elem[i].Type != ACPI_TYPE_INTEGER) {
            xprintf("acpi-battery: unexpected _BIF response\n");
            goto err;
        }
    }
    for (int i = 9; i < 13; i++) {
        if (bif_elem[i].Type != ACPI_TYPE_STRING) {
            xprintf("acpi-battery: unexpected _BIF response\n");
            goto err;
        }
    }

    battery_info_t* info = &dev->battery_info;
    info->unit = bif_elem[0].Integer.Value;
    info->design_capacity = bif_elem[1].Integer.Value;
    info->last_full_capacity = bif_elem[2].Integer.Value;
    info->design_voltage = bif_elem[4].Integer.Value;
    info->capacity_warning = bif_elem[5].Integer.Value;
    info->capacity_low = bif_elem[6].Integer.Value;
    info->capacity_granularity_low_warning = bif_elem[7].Integer.Value;
    info->capacity_granularity_warning_full = bif_elem[8].Integer.Value;

    mtx_unlock(&dev->lock);

    return MX_OK;
err:
    mtx_unlock(&dev->lock);
    return acpi_to_mx_status(acpi_status);
}

static mx_status_t call_BST(acpi_battery_device_t* dev) {
    mtx_lock(&dev->lock);

    ACPI_STATUS acpi_status = AcpiEvaluateObject(dev->acpi_handle,
            (char*)"_BST", NULL, &dev->bst_buffer);
    if (acpi_status != AE_OK) {
        xprintf("acpi-battery: acpi error 0x%x in _BST\n", acpi_status);
        goto err;
    }
    ACPI_OBJECT* bst_pkg = dev->bst_buffer.Pointer;
    if ((bst_pkg->Type != ACPI_TYPE_PACKAGE) || (bst_pkg->Package.Count != 4)) {
        xprintf("acpi-battery: unexpected _BST response\n");
        goto err;
    }
    ACPI_OBJECT* bst_elem = bst_pkg->Package.Elements;
    int i;
    for (i = 0; i < 4; i++) {
        if (bst_elem[i].Type != ACPI_TYPE_INTEGER) {
            xprintf("acpi-battery: unexpected _BST response\n");
            goto err;
        }
    }

    power_info_t* pinfo = &dev->power_info;
    uint32_t old = pinfo->state;
    uint32_t astate = bst_elem[0].Integer.Value;
    if (astate & ACPI_BATTERY_STATE_DISCHARGING) {
        pinfo->state |= POWER_STATE_DISCHARGING;
    } else {
        pinfo->state &= ~POWER_STATE_DISCHARGING;
    }
    if (astate & ACPI_BATTERY_STATE_CHARGING) {
        pinfo->state |= POWER_STATE_CHARGING;
    } else {
        pinfo->state &= ~POWER_STATE_CHARGING;
    }
    if (astate & ACPI_BATTERY_STATE_CRITICAL) {
        pinfo->state |= POWER_STATE_CRITICAL;
    } else {
        pinfo->state &= ~POWER_STATE_CRITICAL;
    }

    battery_info_t* binfo = &dev->battery_info;

    // valid values are 0-0x7fffffff so converting to int32_t is safe
    binfo->present_rate = bst_elem[1].Integer.Value;
    if (!(binfo->present_rate & (1 << 31)) && (astate & ACPI_BATTERY_STATE_DISCHARGING)) {
        binfo->present_rate = bst_elem[1].Integer.Value * -1;
    }

    binfo->remaining_capacity = bst_elem[2].Integer.Value;
    binfo->present_voltage = bst_elem[3].Integer.Value;

    if (old != pinfo->state) {
        mx_object_signal(dev->event, 0, MX_USER_SIGNAL_0);
    }

    mtx_unlock(&dev->lock);

    return MX_OK;
err:
    mtx_unlock(&dev->lock);
    return acpi_to_mx_status(acpi_status);
}

static void acpi_battery_notify(ACPI_HANDLE handle, UINT32 value, void* ctx) {
    acpi_battery_device_t* dev = ctx;
    xprintf("acpi-battery: got event 0x%x\n", value);
    switch (value) {
    case 0x80:
        // battery state has changed
        call_BST(dev);
        break;
    case 0x81:
        // static battery information has changed
        call_STA(dev);
        call_BIF(dev);
        break;
    }
}

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

static mx_status_t acpi_battery_ioctl(void* ctx, uint32_t op,
                                      const void* in_buf, size_t in_len,
                                      void* out_buf, size_t out_len, size_t* out_actual) {
    acpi_battery_device_t* dev = ctx;
    mx_status_t status = MX_ERR_NOT_SUPPORTED;
    switch (op) {
    case IOCTL_POWER_GET_INFO: {
        if (out_len != sizeof(power_info_t)) {
            status = MX_ERR_INVALID_ARGS;
            goto err;
        }

        // reading state clears the signal
        mx_object_signal(dev->event, MX_USER_SIGNAL_0, 0);

        power_info_t* info = (power_info_t*)out_buf;
        mtx_lock(&dev->lock);
        memcpy(info, &dev->power_info, sizeof(power_info_t));
        mtx_unlock(&dev->lock);
        *out_actual = sizeof(power_info_t);
        return MX_OK;
    }
    case IOCTL_POWER_GET_BATTERY_INFO: {
        if (out_len != sizeof(battery_info_t)) {
            status = MX_ERR_INVALID_ARGS;
            goto err;
        }
        call_BST(dev);
        battery_info_t* info = (battery_info_t*)out_buf;
        mtx_lock(&dev->lock);
        memcpy(info, &dev->battery_info, sizeof(battery_info_t));
        mtx_unlock(&dev->lock);
        *out_actual = sizeof(battery_info_t);
        return MX_OK;
    }
    case IOCTL_POWER_GET_STATE_CHANGE_EVENT: {
        if (out_len != sizeof(mx_handle_t)) {
            status = MX_ERR_INVALID_ARGS;
            goto err;
        }
        mx_handle_t* out = (mx_handle_t*)out_buf;
        mx_status_t status = mx_handle_duplicate(dev->event,
                                                 MX_RIGHT_READ | MX_RIGHT_TRANSFER,
                                                 out);
        if (status != MX_OK) {
            goto err;
        }

        // clear the signal before returning
        mx_object_signal(dev->event, MX_USER_SIGNAL_0, 0);
        *out_actual = sizeof(mx_handle_t);
        return MX_OK;
    }
    default:
        status = MX_ERR_NOT_SUPPORTED;
    }
err:
    *out_actual = 0;
    return status;
}

static void acpi_battery_release(void* ctx) {
    acpi_battery_device_t* dev = ctx;
    AcpiRemoveNotifyHandler(dev->acpi_handle, ACPI_DEVICE_NOTIFY, acpi_battery_notify);
    if (dev->bst_buffer.Length != ACPI_ALLOCATE_BUFFER) {
        AcpiOsFree(dev->bst_buffer.Pointer);
    }
    if (dev->bif_buffer.Length != ACPI_ALLOCATE_BUFFER) {
        AcpiOsFree(dev->bif_buffer.Pointer);
    }
    if (dev->event != MX_HANDLE_INVALID) {
        mx_handle_close(dev->event);
    }
    free(dev);
}

static mx_protocol_device_t acpi_battery_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .read = acpi_battery_read,
    .ioctl = acpi_battery_ioctl,
    .release = acpi_battery_release,
};

static int acpi_battery_poll_thread(void* arg) {
    acpi_battery_device_t* dev = arg;
    for (;;) {
        mx_status_t status = call_BST(dev);
        if (status != MX_OK) {
            goto out;
        }

        status = call_BIF(dev);
        if (status != MX_OK) {
            goto out;
        }

        mtx_lock(&dev->lock);
        if (dev->power_info.state & POWER_STATE_DISCHARGING) {
            dev->state |= ACPI_BATTERY_STATE_DISCHARGING;
        } else {
            dev->state &= ~ACPI_BATTERY_STATE_DISCHARGING;
        }
        if (dev->power_info.state & POWER_STATE_CHARGING) {
            dev->state |= ACPI_BATTERY_STATE_CHARGING;
        } else {
            dev->state &= ~ACPI_BATTERY_STATE_CHARGING;
        }
        if (dev->power_info.state & POWER_STATE_CRITICAL) {
            dev->state |= ACPI_BATTERY_STATE_CRITICAL;
        } else {
            dev->state &= ~ACPI_BATTERY_STATE_CRITICAL;
        }
        dev->capacity_remaining = dev->battery_info.remaining_capacity;
        dev->capacity_design = dev->battery_info.design_capacity;
        dev->capacity_full = dev->battery_info.last_full_capacity;
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
    mtx_init(&dev->lock, mtx_plain);

    mx_status_t status = mx_event_create(0, &dev->event);
    if (status != MX_OK) {
        free(dev);
        return status;
    }

    dev->bst_buffer.Length = ACPI_ALLOCATE_BUFFER;
    dev->bst_buffer.Pointer = NULL;

    dev->bif_buffer.Length = ACPI_ALLOCATE_BUFFER;
    dev->bif_buffer.Pointer = NULL;

    dev->power_info.type = POWER_TYPE_BATTERY;

    // get initial values
    call_STA(dev);
    call_BIF(dev);
    call_BST(dev);

    // install acpi event handler
    ACPI_STATUS acpi_status = AcpiInstallNotifyHandler(acpi_handle, ACPI_DEVICE_NOTIFY,
            acpi_battery_notify, dev);
    if (acpi_status != AE_OK) {
        xprintf("acpi-battery: could not install notify handler\n");
        acpi_battery_release(dev);
        return acpi_to_mx_status(acpi_status);
    }

    // deprecated - create polling thread
    int rc = thrd_create_with_name(&dev->poll_thread,
            acpi_battery_poll_thread, dev, "acpi-battery-poll");
    if (rc != thrd_success) {
        xprintf("acpi-battery: polling thread did not start (%d)\n", rc);
        acpi_battery_release(dev);
        return rc;
    }

   device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "acpi-battery",
        .ctx = dev,
        .ops = &acpi_battery_device_proto,
        .proto_id = MX_PROTOCOL_POWER,
    };

    status = device_add(parent, &args, &dev->mxdev);
    if (status != MX_OK) {
        xprintf("acpi-battery: could not add device! err=%d\n", status);
        acpi_battery_release(dev);
        return status;
    }

    // deprecated
   device_add_args_t args2 = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "acpi-battery",
        .ctx = dev,
        .ops = &acpi_battery_device_proto,
        .proto_id = MX_PROTOCOL_BATTERY,
    };

    status = device_add(parent, &args2, NULL);
    if (status != MX_OK) {
        xprintf("acpi-battery: could not add deprecated device! err=%d\n", status);
    }

    printf("acpi-battery: initialized\n");

    return MX_OK;
}
