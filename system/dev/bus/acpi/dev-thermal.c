// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/debug.h>

#include <zircon/types.h>
#include <zircon/device/thermal.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>

#include <acpica/acpi.h>

#include "dev.h"
#include "errors.h"
#include "util.h"

#define INT3403_TYPE_SENSOR    0x03
#define INT3403_THERMAL_EVENT  0x90

typedef struct acpi_thermal_device {
    zx_device_t* zxdev;
    ACPI_HANDLE acpi_handle;

    mtx_t lock;

    // event to notify on
    zx_handle_t event;

    // programmable trip points
    uint32_t trip_point_count;
    uint32_t trip_points[9];
} acpi_thermal_device_t;

static zx_status_t acpi_thermal_get_info(acpi_thermal_device_t* dev, thermal_info_t* info) {
    mtx_lock(&dev->lock);
    zx_status_t st = ZX_OK;

    uint64_t temp;
    ACPI_STATUS acpi_status = acpi_evaluate_integer(dev->acpi_handle, "_PSV", &temp);
    if (acpi_status != AE_OK) {
        st = acpi_to_zx_status(acpi_status);
        goto out;
    }
    info->passive_temp = (uint32_t)temp; // we probably won't exceed 429496456.35 C
    acpi_status = acpi_evaluate_integer(dev->acpi_handle, "_CRT", &temp);
    if (acpi_status != AE_OK) {
        st = acpi_to_zx_status(acpi_status);
        goto out;
    }
    info->critical_temp = (uint32_t)temp;

    info->max_trip_count = dev->trip_point_count;
    memcpy(info->active_trip, dev->trip_points, sizeof(info->active_trip));

    acpi_status = acpi_evaluate_integer(dev->acpi_handle, "_TMP", &temp);
    if (acpi_status != AE_OK) {
        st = acpi_to_zx_status(acpi_status);
        goto out;
    }
    info->state = 0;
    if (info->active_trip[0] && (temp > info->active_trip[0])) {
        info->state |= THERMAL_STATE_TRIP_VIOLATION;
    }
out:
    mtx_unlock(&dev->lock);
    return st;
}

static zx_status_t acpi_thermal_read(void* ctx, void* buf, size_t count, zx_off_t off,
                                     size_t* actual) {
    acpi_thermal_device_t* dev = ctx;
    uint64_t v;

    if (count < sizeof(uint32_t)) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    ACPI_STATUS acpi_status = acpi_evaluate_integer(dev->acpi_handle, "_TMP", &v);
    if (acpi_status != AE_OK) {
        zxlogf(ERROR, "acpi-thermal: acpi error %d in _TMP\n", acpi_status);
        return acpi_to_zx_status(acpi_status);
    }
    uint32_t temp = (uint32_t)v;
    memcpy(buf, &temp, sizeof(temp));
    *actual = sizeof(temp);

    return ZX_OK;
}

static zx_status_t acpi_thermal_ioctl(void* ctx, uint32_t op,
                                      const void* in_buf, size_t in_len,
                                      void* out_buf, size_t out_len, size_t* out_actual) {
    acpi_thermal_device_t* dev = ctx;
    switch (op) {
    case IOCTL_THERMAL_GET_INFO: {
        if (out_len != sizeof(thermal_info_t)) {
            return ZX_ERR_INVALID_ARGS;
        }

        // reading state clears the signal
        zx_object_signal(dev->event, ZX_USER_SIGNAL_0, 0);

        thermal_info_t info;
        zx_status_t status = acpi_thermal_get_info(dev, &info);
        if (status != ZX_OK) {
            return status;
        }
        memcpy(out_buf, &info, sizeof(info));
        *out_actual = sizeof(info);
        return ZX_OK;
    }
    case IOCTL_THERMAL_SET_TRIP: {
        if (in_len != sizeof(trip_point_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        if (dev->trip_point_count < 1) {
            return ZX_ERR_NOT_SUPPORTED;
        }
        trip_point_t* tp = (trip_point_t*)in_buf;
        // only one trip point for now
        if (tp->id != 0) {
            return ZX_ERR_INVALID_ARGS;
        }
        ACPI_STATUS acpi_status = acpi_evaluate_method_intarg(dev->acpi_handle,
                                                              "PAT0", tp->temp);
        if (acpi_status != AE_OK) {
            zxlogf(ERROR, "acpi-thermal: acpi error %d in PAT0\n", acpi_status);
            return acpi_to_zx_status(acpi_status);
        }
        mtx_lock(&dev->lock);
        dev->trip_points[0] = tp->temp;
        mtx_unlock(&dev->lock);
        return ZX_OK;
    }
    case IOCTL_THERMAL_GET_STATE_CHANGE_EVENT: {
        if (out_len != sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        zx_handle_t* out = (zx_handle_t*)out_buf;
        zx_status_t status = zx_handle_duplicate(dev->event, ZX_RIGHT_SAME_RIGHTS, out);
        if (status != ZX_OK) {
            return status;
        }

        // clear the signal before returning
        zx_object_signal(dev->event, ZX_USER_SIGNAL_0, 0);
        *out_actual = sizeof(zx_handle_t);
        return ZX_OK;
    }
    default:
       return ZX_ERR_NOT_SUPPORTED;
    }
}

static void acpi_thermal_notify(ACPI_HANDLE handle, UINT32 value, void* ctx) {
    acpi_thermal_device_t* dev = ctx;
    zxlogf(TRACE, "acpi-thermal: got event 0x%x\n", value);
    switch (value) {
    case INT3403_THERMAL_EVENT:
        zx_object_signal(dev->event, 0, ZX_USER_SIGNAL_0);
        break;
    }
}

static void acpi_thermal_release(void* ctx) {
    acpi_thermal_device_t* dev = ctx;
    AcpiRemoveNotifyHandler(dev->acpi_handle, ACPI_DEVICE_NOTIFY, acpi_thermal_notify);
    zx_handle_close(dev->event);
    free(dev);
}

static zx_protocol_device_t acpi_thermal_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .read = acpi_thermal_read,
    .ioctl = acpi_thermal_ioctl,
    .release = acpi_thermal_release,
};

zx_status_t thermal_init(zx_device_t* parent, ACPI_DEVICE_INFO* info, ACPI_HANDLE acpi_handle) {
    // only support sensors
    uint64_t type;
    ACPI_STATUS acpi_status = acpi_evaluate_integer(acpi_handle, "PTYP", &type);
    if (acpi_status != AE_OK) {
        zxlogf(ERROR, "acpi-thermal: acpi error %d in PTYP\n", acpi_status);
        return acpi_to_zx_status(acpi_status);
    }
    if (type != INT3403_TYPE_SENSOR) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    acpi_thermal_device_t* dev = calloc(1, sizeof(acpi_thermal_device_t));
    if (!dev) {
        return ZX_ERR_NO_MEMORY;
    }

    dev->acpi_handle = acpi_handle;

    zx_status_t status = zx_event_create(0, &dev->event);
    if (status != ZX_OK) {
        zxlogf(ERROR, "acpi-thermal: error %d in zx_event_create\n", status);
        acpi_thermal_release(dev);
        return status;
    }

    // install acpi event handler
    acpi_status = AcpiInstallNotifyHandler(acpi_handle, ACPI_DEVICE_NOTIFY,
                                           acpi_thermal_notify, dev);
    if (acpi_status != AE_OK) {
        zxlogf(ERROR, "acpi-thermal: could not install notify handler\n");
        acpi_thermal_release(dev);
        return acpi_to_zx_status(acpi_status);
    }

    uint64_t v;
    acpi_status = acpi_evaluate_integer(dev->acpi_handle, "PATC", &v);
    if (acpi_status != AE_OK) {
        zxlogf(ERROR, "acpi-thermal: could not get auxiliary trip count\n");
        return acpi_status;
    }
    dev->trip_point_count = (uint32_t)v;

    char name[5];
    memcpy(name, &info->Name, sizeof(UINT32));
    name[4] = '\0';

   device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ctx = dev,
        .ops = &acpi_thermal_device_proto,
        .proto_id = ZX_PROTOCOL_THERMAL,
    };

    status = device_add(parent, &args, &dev->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "acpi-thermal: could not add device! err=%d\n", status);
        acpi_thermal_release(dev);
        return status;
    }

    zxlogf(TRACE, "acpi-thermal: initialized '%s' %u trip points\n",
           name, dev->trip_point_count);

    return ZX_OK;
}
