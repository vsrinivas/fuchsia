// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

// function pointer for testability, used to mock out AcpiEvaluateObject where necessary
typedef ACPI_STATUS (*AcpiObjectEvalFunc)(ACPI_HANDLE, char*, ACPI_OBJECT_LIST*, ACPI_BUFFER*);

typedef struct acpi_pwrsrc_device {
  zx_device_t* zxdev;

  ACPI_HANDLE acpi_handle;

  // event to notify on
  zx_handle_t event;

  power_info_t info;

  mtx_t lock;

  AcpiObjectEvalFunc acpi_eval;

} acpi_pwrsrc_device_t;

static zx_status_t call_PSR(acpi_pwrsrc_device_t* dev) {
  ACPI_OBJECT obj = {
      .Type = ACPI_TYPE_INTEGER,
  };
  ACPI_BUFFER buffer = {
      .Length = sizeof(obj),
      .Pointer = &obj,
  };
  ACPI_STATUS acpi_status = dev->acpi_eval(dev->acpi_handle, (char*)"_PSR", NULL, &buffer);
  if (acpi_status == AE_OK) {
    mtx_lock(&dev->lock);
    uint32_t state = dev->info.state;
    if (obj.Integer.Value) {
      dev->info.state |= POWER_STATE_ONLINE;
    } else {
      dev->info.state &= ~POWER_STATE_ONLINE;
    }
    zxlogf(TRACE, "acpi-pwrsrc: call_PSR state change 0x%x -> 0x%x\n", state, dev->info.state);
    if (state != dev->info.state) {
      zx_object_signal(dev->event, 0, ZX_USER_SIGNAL_0);
    }
    mtx_unlock(&dev->lock);
  }
  return acpi_to_zx_status(acpi_status);
}

static void acpi_pwrsrc_notify(ACPI_HANDLE handle, UINT32 value, void* ctx) {
  acpi_pwrsrc_device_t* dev = ctx;
  zxlogf(TRACE, "acpi-pwrsrc: notify got event 0x%x\n", value);
  call_PSR(dev);
}

static void acpi_pwrsrc_release(void* ctx) {
  acpi_pwrsrc_device_t* dev = ctx;
  AcpiRemoveNotifyHandler(dev->acpi_handle, ACPI_DEVICE_NOTIFY, acpi_pwrsrc_notify);
  if (dev->event != ZX_HANDLE_INVALID) {
    zx_handle_close(dev->event);
  }
  free(dev);
}

zx_status_t fidl_pwrsrc_get_power_info(void* ctx, fidl_txn_t* txn) {
  acpi_pwrsrc_device_t* dev = ctx;
  struct fuchsia_hardware_power_SourceInfo info;

  mtx_lock(&dev->lock);
  info.state = dev->info.state;
  info.type = dev->info.type;
  mtx_unlock(&dev->lock);

  // reading state clears the signal
  zx_object_signal(dev->event, ZX_USER_SIGNAL_0, 0);
  return fuchsia_hardware_power_SourceGetPowerInfo_reply(txn, ZX_OK, &info);
}

zx_status_t fidl_pwrsrc_get_state_change_event(void* ctx, fidl_txn_t* txn) {
  acpi_pwrsrc_device_t* dev = ctx;
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
    .GetPowerInfo = fidl_pwrsrc_get_power_info,
    .GetStateChangeEvent = fidl_pwrsrc_get_state_change_event,
};

static zx_status_t fuchsia_hardware_power_message_instance(void* ctx, fidl_msg_t* msg,
                                                           fidl_txn_t* txn) {
  return fuchsia_hardware_power_Source_dispatch(ctx, txn, msg, &fidl_ops);
}

static zx_protocol_device_t acpi_pwrsrc_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .message = fuchsia_hardware_power_message_instance,
    .release = acpi_pwrsrc_release,
};

zx_status_t pwrsrc_init(zx_device_t* parent, ACPI_HANDLE acpi_handle) {
  // driver trace logging can be enabled for debug as needed
  // driver_set_log_flags(driver_get_log_flags() | DDK_LOG_TRACE);

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

  // use real AcpiEvaluateObject
  dev->acpi_eval = &AcpiEvaluateObject;

  ACPI_STATUS acpi_status =
      AcpiInstallNotifyHandler(acpi_handle, ACPI_DEVICE_NOTIFY, acpi_pwrsrc_notify, dev);

  call_PSR(dev);

  if (acpi_status != AE_OK) {
    zxlogf(ERROR, "acpi-pwrsrc: could not install notify handler\n");
    acpi_pwrsrc_release(dev);
    return acpi_to_zx_status(acpi_status);
  }

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
