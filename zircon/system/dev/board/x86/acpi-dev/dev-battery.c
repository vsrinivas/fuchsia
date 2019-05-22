// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <fuchsia/hardware/power/c/fidl.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <acpica/acpi.h>

#include "dev.h"
#include "errors.h"
#include "power.h"

#define ACPI_BATTERY_STATE_DISCHARGING (1 << 0)
#define ACPI_BATTERY_STATE_CHARGING (1 << 1)
#define ACPI_BATTERY_STATE_CRITICAL (1 << 2)

// function pointer for testability, used to mock out AcpiEvaluateObject where necessary
typedef ACPI_STATUS (*AcpiObjectEvalFunc)(ACPI_HANDLE, char*, ACPI_OBJECT_LIST*, ACPI_BUFFER*);

typedef struct acpi_battery_device {
    zx_device_t* zxdev;

    ACPI_HANDLE acpi_handle;

    ACPI_BUFFER bst_buffer;
    ACPI_BUFFER bif_buffer;

    // thread to poll for battery status
    thrd_t poll_thread;

    mtx_t lock;

    // event to notify on
    zx_handle_t event;

    power_info_t power_info;
    battery_info_t battery_info;

    atomic_bool shutdown;

    AcpiObjectEvalFunc acpi_eval;
} acpi_battery_device_t;

// Returns the current ON/OFF status for a power resource
static zx_status_t call_STA(acpi_battery_device_t* dev) {
    ACPI_OBJECT obj = {
        .Type = ACPI_TYPE_INTEGER,
    };
    ACPI_BUFFER buffer = {
        .Length = sizeof(obj),
        .Pointer = &obj,
    };
    ACPI_STATUS acpi_status = dev->acpi_eval(dev->acpi_handle, (char*)"_STA", NULL, &buffer);
    if (acpi_status != AE_OK) {
        return acpi_to_zx_status(acpi_status);
    }

    zxlogf(TRACE, "acpi-battery: _STA returned 0x%llx\n", obj.Integer.Value);

    mtx_lock(&dev->lock);
    uint32_t old = dev->power_info.state;
    if (obj.Integer.Value & ACPI_STA_BATTERY_PRESENT) {
        dev->power_info.state |= POWER_STATE_ONLINE;
    } else {
        dev->power_info.state &= ~POWER_STATE_ONLINE;
    }

    if (old != dev->power_info.state) {
        zx_object_signal(dev->event, 0, ZX_USER_SIGNAL_0);
    }
    mtx_unlock(&dev->lock);
    return ZX_OK;
}

static zx_status_t call_BIF(acpi_battery_device_t* dev) {
    mtx_lock(&dev->lock);

    ACPI_STATUS acpi_status = dev->acpi_eval(dev->acpi_handle,
                                                 (char*)"_BIF", NULL, &dev->bif_buffer);
    if (acpi_status != AE_OK) {
        zxlogf(TRACE, "acpi-battery: acpi error 0x%x in _BIF\n", acpi_status);
        goto err;
    }
    ACPI_OBJECT* bif_pkg = dev->bif_buffer.Pointer;
    if ((bif_pkg->Type != ACPI_TYPE_PACKAGE) || (bif_pkg->Package.Count != 13)) {
        zxlogf(TRACE, "acpi-battery: unexpected _BIF response\n");
        goto err;
    }
    ACPI_OBJECT* bif_elem = bif_pkg->Package.Elements;
    for (int i = 0; i < 9; i++) {
        if (bif_elem[i].Type != ACPI_TYPE_INTEGER) {
            zxlogf(TRACE, "acpi-battery: unexpected _BIF response\n");
            goto err;
        }
    }
    for (int i = 9; i < 13; i++) {
        if (bif_elem[i].Type != ACPI_TYPE_STRING) {
            zxlogf(TRACE, "acpi-battery: unexpected _BIF response\n");
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

    return ZX_OK;
err:
    mtx_unlock(&dev->lock);
    return acpi_to_zx_status(acpi_status);
}

static zx_status_t call_BST(acpi_battery_device_t* dev) {
    mtx_lock(&dev->lock);

    ACPI_STATUS acpi_status = dev->acpi_eval(dev->acpi_handle,
                                                 (char*)"_BST", NULL, &dev->bst_buffer);
    if (acpi_status != AE_OK) {
        zxlogf(TRACE, "acpi-battery: acpi error 0x%x in _BST\n", acpi_status);
        goto err;
    }
    ACPI_OBJECT* bst_pkg = dev->bst_buffer.Pointer;
    if ((bst_pkg->Type != ACPI_TYPE_PACKAGE) || (bst_pkg->Package.Count != 4)) {
        zxlogf(TRACE, "acpi-battery: unexpected _BST response\n");
        goto err;
    }
    ACPI_OBJECT* bst_elem = bst_pkg->Package.Elements;
    int i;
    for (i = 0; i < 4; i++) {
        if (bst_elem[i].Type != ACPI_TYPE_INTEGER) {
            zxlogf(TRACE, "acpi-battery: unexpected _BST response\n");
            goto err;
        }
    }

    power_info_t* pinfo = &dev->power_info;
    uint32_t old_state = pinfo->state;
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

    uint32_t old_charge = binfo->remaining_capacity;
    if (binfo->last_full_capacity) {
        old_charge = (binfo->remaining_capacity * 100 / binfo->last_full_capacity);
    }

    binfo->remaining_capacity = bst_elem[2].Integer.Value;
    binfo->present_voltage = bst_elem[3].Integer.Value;

    uint32_t new_charge = binfo->remaining_capacity;
    if (binfo->last_full_capacity) {
        new_charge = (binfo->remaining_capacity * 100 / binfo->last_full_capacity);
    }

    // signal on change of charging state (e.g charging vs discharging) as well as significant
    // change in charge (percentage point).
    if (old_state != pinfo->state || old_charge != new_charge) {
        if (old_state != pinfo->state) {
        zxlogf(TRACE, "acpi-battery: state 0x%x -> 0x%x\n", old_state, pinfo->state);
        }
        if (old_charge != new_charge) {
        zxlogf(TRACE, "acpi-battery: %% charged %d -> %d\n",
                                    old_charge, new_charge);
        }
        zx_object_signal(dev->event, 0, ZX_USER_SIGNAL_0);
    }

    mtx_unlock(&dev->lock);

    return ZX_OK;
err:
    mtx_unlock(&dev->lock);
    return acpi_to_zx_status(acpi_status);
}

static void acpi_battery_notify(ACPI_HANDLE handle, UINT32 value, void* ctx) {
    acpi_battery_device_t* dev = ctx;
    zxlogf(TRACE, "acpi-battery: got event 0x%x\n", value);
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

static void acpi_battery_release(void* ctx) {
    acpi_battery_device_t* dev = ctx;
    atomic_store(&dev->shutdown, true);
    thrd_join(dev->poll_thread, NULL);

    AcpiRemoveNotifyHandler(dev->acpi_handle, ACPI_DEVICE_NOTIFY, acpi_battery_notify);
    if (dev->bst_buffer.Length != ACPI_ALLOCATE_BUFFER) {
        AcpiOsFree(dev->bst_buffer.Pointer);
    }
    if (dev->bif_buffer.Length != ACPI_ALLOCATE_BUFFER) {
        AcpiOsFree(dev->bif_buffer.Pointer);
    }
    if (dev->event != ZX_HANDLE_INVALID) {
        zx_handle_close(dev->event);
    }
    free(dev);
}

static zx_status_t acpi_battery_suspend(void* ctx, uint32_t flags) {
    acpi_battery_device_t* dev = ctx;

    if (flags != DEVICE_SUSPEND_FLAG_MEXEC) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    atomic_store(&dev->shutdown, true);
    thrd_join(dev->poll_thread, NULL);
    return ZX_OK;
}

zx_status_t fidl_battery_get_power_info(void* ctx, fidl_txn_t* txn) {
    acpi_battery_device_t* dev = ctx;
    struct fuchsia_hardware_power_SourceInfo info;

    mtx_lock(&dev->lock);
    info.state = dev->power_info.state;
    info.type = dev->power_info.type;
    mtx_unlock(&dev->lock);

    // reading state clears the signal
    zx_object_signal(dev->event, ZX_USER_SIGNAL_0, 0);
    return fuchsia_hardware_power_SourceGetPowerInfo_reply(txn, ZX_OK, &info);
}

zx_status_t fidl_battery_get_battery_info(void* ctx, fidl_txn_t* txn) {
    acpi_battery_device_t* dev = ctx;
    zx_status_t status = call_BST(dev);
    struct fuchsia_hardware_power_BatteryInfo info = {};

    if (status == ZX_OK) {
        mtx_lock(&dev->lock);
        info.unit = dev->battery_info.unit;
        info.design_capacity = dev->battery_info.design_capacity;
        info.last_full_capacity = dev->battery_info.last_full_capacity;
        info.design_voltage = dev->battery_info.design_voltage;
        info.capacity_warning = dev->battery_info.capacity_warning;
        info.capacity_low = dev->battery_info.capacity_low;
        info.capacity_granularity_low_warning = dev->battery_info.capacity_granularity_low_warning;
        info.capacity_granularity_warning_full = dev->battery_info.capacity_granularity_warning_full;
        info.present_rate = dev->battery_info.present_rate;
        info.remaining_capacity = dev->battery_info.remaining_capacity;
        info.present_voltage = dev->battery_info.present_voltage;
        mtx_unlock(&dev->lock);
    }

    return fuchsia_hardware_power_SourceGetBatteryInfo_reply(txn, status, &info);
}

zx_status_t fidl_battery_get_state_change_event(void* ctx, fidl_txn_t* txn) {
    acpi_battery_device_t* dev = ctx;
    zx_handle_t out_handle;
    zx_rights_t rights = ZX_RIGHT_WAIT | ZX_RIGHT_TRANSFER;
    zx_status_t status = zx_handle_duplicate(dev->event, rights, &out_handle);

    if (status == ZX_OK) {
        // clear the signal before returning
        zx_object_signal(dev->event, ZX_USER_SIGNAL_0, 0);
    }

    return fuchsia_hardware_power_SourceGetStateChangeEvent_reply(txn, status, out_handle);
}

static fuchsia_hardware_power_Source_ops_t fidl_ops = {
    .GetPowerInfo = fidl_battery_get_power_info,
    .GetStateChangeEvent = fidl_battery_get_state_change_event,
    .GetBatteryInfo = fidl_battery_get_battery_info,
};

static zx_status_t fuchsia_battery_message_instance(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
    return fuchsia_hardware_power_Source_dispatch(ctx, txn, msg, &fidl_ops);
}

static zx_protocol_device_t acpi_battery_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .message = fuchsia_battery_message_instance,
    .release = acpi_battery_release,
    .suspend = acpi_battery_suspend,
};

static int acpi_battery_poll_thread(void* arg) {
    acpi_battery_device_t* dev = arg;
    while (!atomic_load(&dev->shutdown)) {
        zx_status_t status = call_BST(dev);
        if (status != ZX_OK) {
            goto out;
        }

        status = call_BIF(dev);
        if (status != ZX_OK) {
            goto out;
        }

        zx_nanosleep(zx_deadline_after(ZX_MSEC(1000)));
    }
out:
    zxlogf(TRACE, "acpi-battery: poll thread exiting\n");
    return 0;
}

zx_status_t battery_init(zx_device_t* parent, ACPI_HANDLE acpi_handle) {

    // driver trace logging can be enabled for debug as needed
    // driver_set_log_flags(driver_get_log_flags() | DDK_LOG_TRACE);

    zxlogf(TRACE, "acpi-battery: init with ACPI_HANDLE %p\n", acpi_handle);

    ACPI_BUFFER name_buffer;
    name_buffer.Length = ACPI_ALLOCATE_BUFFER;
    name_buffer.Pointer = NULL;

    AcpiGetName(acpi_handle, ACPI_FULL_PATHNAME, &name_buffer);

    zxlogf(TRACE, "acpi-battery: path for acpi handle is %s\n", (char *)name_buffer.Pointer);

    acpi_battery_device_t* dev = calloc(1, sizeof(acpi_battery_device_t));
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

    dev->bst_buffer.Length = ACPI_ALLOCATE_BUFFER;
    dev->bst_buffer.Pointer = NULL;

    dev->bif_buffer.Length = ACPI_ALLOCATE_BUFFER;
    dev->bif_buffer.Pointer = NULL;

    dev->power_info.type = POWER_TYPE_BATTERY;

    // use real AcpiEvaluateObject
    dev->acpi_eval = &AcpiEvaluateObject;

    // get initial values
    call_STA(dev);
    call_BIF(dev);
    call_BST(dev);

    // install acpi event handler
    ACPI_STATUS acpi_status = AcpiInstallNotifyHandler(acpi_handle, ACPI_DEVICE_NOTIFY,
                                                       acpi_battery_notify, dev);
    if (acpi_status != AE_OK) {
        zxlogf(ERROR, "acpi-battery: could not install notify handler\n");
        acpi_battery_release(dev);
        return acpi_to_zx_status(acpi_status);
    }

    // deprecated - create polling thread
    int rc = thrd_create_with_name(&dev->poll_thread,
                                   acpi_battery_poll_thread, dev, "acpi-battery-poll");
    if (rc != thrd_success) {
        zxlogf(ERROR, "acpi-battery: polling thread did not start (%d)\n", rc);
        acpi_battery_release(dev);
        return rc;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "acpi-battery",
        .ctx = dev,
        .ops = &acpi_battery_device_proto,
        .proto_id = ZX_PROTOCOL_POWER,
    };

    status = device_add(parent, &args, &dev->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "acpi-battery: could not add device! err=%d\n", status);
        acpi_battery_release(dev);
        return status;
    }

    zxlogf(TRACE, "acpi-battery: initialized device %s\n", device_get_name(dev->zxdev));

    return ZX_OK;
}
