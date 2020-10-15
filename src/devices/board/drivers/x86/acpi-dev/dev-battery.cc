// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dev-battery.h"

#include <fuchsia/hardware/power/c/fidl.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <acpica/acpi.h>
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include "dev.h"
#include "errors.h"
#include "power.h"

#define ACPI_BATTERY_STATE_DISCHARGING (1 << 0)
#define ACPI_BATTERY_STATE_CHARGING (1 << 1)
#define ACPI_BATTERY_STATE_CRITICAL (1 << 2)

namespace acpi_battery {

// Returns the current ON/OFF status for a power resource
zx_status_t call_STA(acpi_battery_device_t* dev) {
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

  zxlogf(DEBUG, "acpi-battery: _STA returned 0x%llx", obj.Integer.Value);

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

  ACPI_STATUS acpi_status = dev->acpi_eval(dev->acpi_handle, (char*)"_BIF", NULL, &dev->bif_buffer);
  ACPI_OBJECT* bif_elem;
  ACPI_OBJECT* bif_pkg;
  if (acpi_status != AE_OK) {
    zxlogf(DEBUG, "acpi-battery: acpi error 0x%x in _BIF", acpi_status);
    goto err;
  }
  bif_pkg = static_cast<ACPI_OBJECT*>(dev->bif_buffer.Pointer);
  if ((bif_pkg->Type != ACPI_TYPE_PACKAGE) || (bif_pkg->Package.Count != 13)) {
    zxlogf(DEBUG, "acpi-battery: unexpected _BIF response");
    goto err;
  }
  bif_elem = bif_pkg->Package.Elements;
  for (int i = 0; i < 9; i++) {
    if (bif_elem[i].Type != ACPI_TYPE_INTEGER) {
      zxlogf(DEBUG, "acpi-battery: unexpected _BIF response");
      goto err;
    }
  }
  for (int i = 9; i < 13; i++) {
    if (bif_elem[i].Type != ACPI_TYPE_STRING) {
      zxlogf(DEBUG, "acpi-battery: unexpected _BIF response");
      goto err;
    }
  }

  {
    battery_info_t* info = &dev->battery_info;
    info->unit = static_cast<uint32_t>(bif_elem[0].Integer.Value);
    info->design_capacity = static_cast<uint32_t>(bif_elem[1].Integer.Value);
    info->last_full_capacity = static_cast<uint32_t>(bif_elem[2].Integer.Value);
    info->design_voltage = static_cast<uint32_t>(bif_elem[4].Integer.Value);
    info->capacity_warning = static_cast<uint32_t>(bif_elem[5].Integer.Value);
    info->capacity_low = static_cast<uint32_t>(bif_elem[6].Integer.Value);
    info->capacity_granularity_low_warning = static_cast<uint32_t>(bif_elem[7].Integer.Value);
    info->capacity_granularity_warning_full = static_cast<uint32_t>(bif_elem[8].Integer.Value);
  }

  mtx_unlock(&dev->lock);

  return ZX_OK;
err:
  mtx_unlock(&dev->lock);
  return acpi_to_zx_status(acpi_status);
}

zx_status_t call_BST(acpi_battery_device_t* dev) {
  mtx_lock(&dev->lock);

  ACPI_STATUS acpi_status = dev->acpi_eval(dev->acpi_handle, (char*)"_BST", NULL, &dev->bst_buffer);
  ACPI_OBJECT* bst_pkg;
  ACPI_OBJECT* bst_elem;
  power_info_t* pinfo;
  uint32_t old_state;
  uint32_t astate;
  battery_info_t* binfo;
  uint32_t old_charge;
  uint32_t new_charge;
  if (acpi_status != AE_OK) {
    zxlogf(DEBUG, "acpi-battery: acpi error 0x%x in _BST", acpi_status);
    goto err;
  }
  bst_pkg = static_cast<ACPI_OBJECT*>(dev->bst_buffer.Pointer);
  if ((bst_pkg->Type != ACPI_TYPE_PACKAGE) || (bst_pkg->Package.Count != 4)) {
    zxlogf(DEBUG, "acpi-battery: unexpected _BST response");
    goto err;
  }
  bst_elem = static_cast<ACPI_OBJECT*>(bst_pkg->Package.Elements);
  int i;
  for (i = 0; i < 4; i++) {
    if (bst_elem[i].Type != ACPI_TYPE_INTEGER) {
      zxlogf(DEBUG, "acpi-battery: unexpected _BST response");
      goto err;
    }
  }

  pinfo = &dev->power_info;
  old_state = pinfo->state;
  astate = static_cast<uint32_t>(bst_elem[0].Integer.Value);
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

  binfo = &dev->battery_info;

  // valid values are 0-0x7fffffff so converting to int32_t is safe
  binfo->present_rate = static_cast<int32_t>(bst_elem[1].Integer.Value);
  if (!(binfo->present_rate & (1 << 31)) && (astate & ACPI_BATTERY_STATE_DISCHARGING)) {
    binfo->present_rate = static_cast<int32_t>(bst_elem[1].Integer.Value * -1);
  }

  old_charge = binfo->remaining_capacity;
  if (binfo->last_full_capacity) {
    old_charge = (binfo->remaining_capacity * 100 / binfo->last_full_capacity);
  }

  binfo->remaining_capacity = static_cast<uint32_t>(bst_elem[2].Integer.Value);
  binfo->present_voltage = static_cast<uint32_t>(bst_elem[3].Integer.Value);

  new_charge = binfo->remaining_capacity;
  if (binfo->last_full_capacity) {
    new_charge = (binfo->remaining_capacity * 100 / binfo->last_full_capacity);
  }

  // signal on change of charging state (e.g charging vs discharging) as well as significant
  // change in charge (percentage point).
  if (old_state != pinfo->state || old_charge != new_charge) {
    if (old_state != pinfo->state) {
      zxlogf(DEBUG, "acpi-battery: state 0x%x -> 0x%x", old_state, pinfo->state);
    }
    if (old_charge != new_charge) {
      zxlogf(DEBUG, "acpi-battery: %% charged %d -> %d", old_charge, new_charge);
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
  acpi_battery_device_t* dev = static_cast<acpi_battery_device_t*>(ctx);
  zx_time_t timestamp;

  zxlogf(DEBUG, "acpi-battery: got event 0x%x", value);
  switch (value) {
    case 0x80:
      timestamp = zx_clock_get_monotonic();
      if (timestamp < (dev->last_notify_timestamp + ZX_MSEC(ACPI_EVENT_NOTIFY_LIMIT_MS))) {
        // Rate limiting is required here due to some ACPI EC implementations
        // that trigger event notification directly from evaluation that occurs
        // in call_BST, which would otherwise create an infinite loop.
        zxlogf(DEBUG, "acpi-battery: rate limiting event 0x%x", value);
        return;
      }
      // battery state has changed
      call_BST(dev);
      dev->last_notify_timestamp = timestamp;
      break;
    case 0x81:
      // static battery information has changed
      call_STA(dev);
      call_BIF(dev);
      break;
  }
}

static void acpi_battery_release(void* ctx) {
  acpi_battery_device_t* dev = static_cast<acpi_battery_device_t*>(ctx);
  atomic_store(&dev->shutdown, true);

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

static void acpi_battery_suspend(void* ctx, uint8_t requested_state, bool enable_wake,
                                 uint8_t suspend_reason) {
  acpi_battery_device_t* dev = static_cast<acpi_battery_device_t*>(ctx);

  if (suspend_reason != DEVICE_SUSPEND_REASON_MEXEC) {
    device_suspend_reply(dev->zxdev, ZX_ERR_NOT_SUPPORTED, DEV_POWER_STATE_D0);
    return;
  }

  atomic_store(&dev->shutdown, true);
  device_suspend_reply(dev->zxdev, ZX_OK, requested_state);
}

zx_status_t fidl_battery_get_power_info(void* ctx, fidl_txn_t* txn) {
  acpi_battery_device_t* dev = static_cast<acpi_battery_device_t*>(ctx);
  struct fuchsia_hardware_power_SourceInfo info;

  mtx_lock(&dev->lock);
  info.state = static_cast<uint8_t>(dev->power_info.state);
  info.type = static_cast<fuchsia_hardware_power_PowerType>(dev->power_info.type);
  mtx_unlock(&dev->lock);

  // reading state clears the signal
  zx_object_signal(dev->event, ZX_USER_SIGNAL_0, 0);
  return fuchsia_hardware_power_SourceGetPowerInfo_reply(txn, ZX_OK, &info);
}

zx_status_t fidl_battery_get_battery_info(void* ctx, fidl_txn_t* txn) {
  acpi_battery_device_t* dev = static_cast<acpi_battery_device_t*>(ctx);
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
  acpi_battery_device_t* dev = static_cast<acpi_battery_device_t*>(ctx);
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

static zx_status_t fuchsia_battery_message_instance(void* ctx, fidl_incoming_msg_t* msg,
                                                    fidl_txn_t* txn) {
  return fuchsia_hardware_power_Source_dispatch(ctx, txn, msg, &fidl_ops);
}

static zx_protocol_device_t acpi_battery_device_proto = [] {
  zx_protocol_device_t ops = {};
  ops.version = DEVICE_OPS_VERSION;
  ops.release = acpi_battery_release;
  ops.suspend = acpi_battery_suspend;
  ops.message = fuchsia_battery_message_instance;
  return ops;
}();

}  // namespace acpi_battery

zx_status_t battery_init(zx_device_t* parent, ACPI_HANDLE acpi_handle) {
  zxlogf(DEBUG, "acpi-battery: init with ACPI_HANDLE %p", acpi_handle);

  ACPI_BUFFER name_buffer;
  name_buffer.Length = ACPI_ALLOCATE_BUFFER;
  name_buffer.Pointer = NULL;

  AcpiGetName(acpi_handle, ACPI_FULL_PATHNAME, &name_buffer);

  zxlogf(DEBUG, "acpi-battery: path for acpi handle is %s", (char*)name_buffer.Pointer);

  acpi_battery::acpi_battery_device_t* dev = static_cast<acpi_battery::acpi_battery_device_t*>(
      calloc(1, sizeof(acpi_battery::acpi_battery_device_t)));
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

  dev->last_notify_timestamp = 0;

  // get initial values
  call_STA(dev);
  call_BIF(dev);
  call_BST(dev);

  // install acpi event handler
  ACPI_STATUS acpi_status = AcpiInstallNotifyHandler(acpi_handle, ACPI_DEVICE_NOTIFY,
                                                     acpi_battery::acpi_battery_notify, dev);
  if (acpi_status != AE_OK) {
    zxlogf(ERROR, "acpi-battery: could not install notify handler");
    acpi_battery_release(dev);
    return acpi_to_zx_status(acpi_status);
  }

  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "acpi-battery";
  args.ctx = dev;
  args.ops = &acpi_battery::acpi_battery_device_proto;
  args.proto_id = ZX_PROTOCOL_POWER;

  status = device_add(parent, &args, &dev->zxdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "acpi-battery: could not add device! err=%d", status);
    acpi_battery_release(dev);
    return status;
  }

  zxlogf(DEBUG, "acpi-battery: initialized device %s", device_get_name(dev->zxdev));

  return ZX_OK;
}
