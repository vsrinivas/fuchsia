// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/debug.h>

#include <zircon/types.h>
#include <zircon/syscalls.h>
#include <stdlib.h>
#include <stdio.h>
#include <threads.h>

#include <acpica/acpi.h>
#include <zircon/device/power.h>

#include "dev.h"
#include "errors.h"

typedef struct acpi_pwrsrc_device {
    zx_device_t* zxdev;

    ACPI_HANDLE acpi_handle;

    // event to notify on
    zx_handle_t event;

    power_info_t info;

    mtx_t lock;
} acpi_pwrsrc_device_t;

static zx_status_t call_PSR(acpi_pwrsrc_device_t* dev, bool notify) {
    ACPI_OBJECT obj = {
        .Type = ACPI_TYPE_INTEGER,
    };
    ACPI_BUFFER buffer = {
        .Length = sizeof(obj),
        .Pointer = &obj,
    };
    ACPI_STATUS acpi_status = AcpiEvaluateObject(dev->acpi_handle, (char*)"_PSR", NULL, &buffer);
    if (acpi_status == AE_OK) {
        mtx_lock(&dev->lock);
        uint32_t state = dev->info.state;
        if (obj.Integer.Value) {
            dev->info.state |= POWER_STATE_ONLINE;
        } else {
            dev->info.state &= ~POWER_STATE_ONLINE;
        }
        zxlogf(DEBUG1, "acpi-pwrsrc: 0x%x -> 0x%x\n", state, dev->info.state);
        if (notify && (state != dev->info.state)) {
            zx_object_signal(dev->event, 0, ZX_USER_SIGNAL_0);
        }
        mtx_unlock(&dev->lock);
    }
    return acpi_to_zx_status(acpi_status);
}

static void acpi_pwrsrc_notify(ACPI_HANDLE handle, UINT32 value, void* ctx) {
    acpi_pwrsrc_device_t* dev = ctx;
    zxlogf(TRACE, "acpi-pwrsrc: got event 0x%x\n", value);
    call_PSR(dev, true);
}

static zx_status_t acpi_pwrsrc_ioctl(void* ctx, uint32_t op,
                                     const void* in_buf, size_t in_len,
                                     void* out_buf, size_t out_len, size_t* out_actual) {
    acpi_pwrsrc_device_t* dev = ctx;
    zx_status_t status = ZX_ERR_NOT_SUPPORTED;
    switch (op) {
    case IOCTL_POWER_GET_INFO: {
        if (out_len != sizeof(power_info_t)) {
            status = ZX_ERR_INVALID_ARGS;
            goto err;
        }

        // reading state clears the signal
        zx_object_signal(dev->event, ZX_USER_SIGNAL_0, 0);

        power_info_t* info = (power_info_t*)out_buf;
        mtx_lock(&dev->lock);
        memcpy(info, &dev->info, sizeof(power_info_t));
        mtx_unlock(&dev->lock);
        *out_actual = sizeof(power_info_t);
        return ZX_OK;
    }
    case IOCTL_POWER_GET_STATE_CHANGE_EVENT: {
        if (out_len != sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        zx_handle_t* out = (zx_handle_t*)out_buf;
        zx_status_t status = zx_handle_duplicate(dev->event,
                                                 ZX_RIGHT_READ | ZX_RIGHT_WAIT | ZX_RIGHT_TRANSFER,
                                                 out);
        if (status != ZX_OK) {
            goto err;
        }
        // clear the signal before returning
        zx_object_signal(dev->event, ZX_USER_SIGNAL_0, 0);
        *out_actual = sizeof(zx_handle_t);
        return ZX_OK;
    }
    }
err:
    *out_actual = 0;
    return status;
}

static void acpi_pwrsrc_release(void* ctx) {
    acpi_pwrsrc_device_t* dev = ctx;
    AcpiRemoveNotifyHandler(dev->acpi_handle, ACPI_DEVICE_NOTIFY, acpi_pwrsrc_notify);
    if (dev->event != ZX_HANDLE_INVALID) {
        zx_handle_close(dev->event);
    }
    free(dev);
}

static zx_protocol_device_t acpi_pwrsrc_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = acpi_pwrsrc_ioctl,
    .release = acpi_pwrsrc_release,
};

zx_status_t pwrsrc_init(zx_device_t* parent, ACPI_HANDLE acpi_handle) {
    acpi_pwrsrc_device_t* dev = calloc(1, sizeof(acpi_pwrsrc_device_t));
    if (!dev) {
        return ZX_ERR_NO_MEMORY;
    }

    dev->acpi_handle = acpi_handle;
    mtx_init(&dev->lock, mtx_plain);

    zx_status_t status = zx_event_create(0, &dev->event);
    if (status != ZX_OK) {
        free(dev);
        return status;
    }

    dev->info.type = POWER_TYPE_AC;
    call_PSR(dev, false);

    ACPI_STATUS acpi_status = AcpiInstallNotifyHandler(acpi_handle, ACPI_DEVICE_NOTIFY,
            acpi_pwrsrc_notify, dev);
    if (acpi_status != AE_OK) {
        zxlogf(ERROR, "acpi-pwrsrc: could not install notify handler\n");
        acpi_pwrsrc_release(dev);
        return acpi_to_zx_status(acpi_status);
    }

    // read initial value
    acpi_pwrsrc_notify(acpi_handle, 0, dev);

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "acpi-pwrsrc",
        .ctx = dev,
        .ops = &acpi_pwrsrc_device_proto,
        .proto_id = ZX_PROTOCOL_POWER,
    };

    status = device_add(parent, &args, &dev->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "acpi-pwrsrc: could not add device! err=%d\n", status);
        acpi_pwrsrc_release(dev);
        return status;
    }

    zxlogf(TRACE, "acpi-pwrsrc: initialized\n");

    return ZX_OK;
}
