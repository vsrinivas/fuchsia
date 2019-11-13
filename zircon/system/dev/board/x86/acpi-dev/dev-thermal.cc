// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dev-thermal.h"

#include <fuchsia/hardware/thermal/c/fidl.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/types.h>

#include <acpica/acpi.h>
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include "dev.h"
#include "errors.h"
#include "methods.h"
#include "util.h"

#define INT3403_TYPE_SENSOR 0x03
#define INT3403_THERMAL_EVENT 0x90

#define KELVIN_CELSIUS_OFFSET 273.15f

namespace acpi_thermal {

static inline float decikelvin_to_celsius(uint64_t temp_decikelvin) {
  return ((float)temp_decikelvin / 10.0f) - KELVIN_CELSIUS_OFFSET;
}

static inline uint64_t celsius_to_decikelvin(float temp_celsius) {
  return (uint64_t)roundf((temp_celsius + KELVIN_CELSIUS_OFFSET) * 10.0f);
}

static zx_status_t acpi_thermal_get_info(acpi_thermal_device_t* dev,
                                         fuchsia_hardware_thermal_ThermalInfo* info) {
  mtx_lock(&dev->lock);
  zx_status_t st = ZX_OK;

  uint64_t temp = 0.0f;
  st = acpi_psv_call(dev->acpi_handle, &temp);
  if (st != ZX_OK) {
    goto out;
  }
  info->passive_temp_celsius = decikelvin_to_celsius(temp);
  st = acpi_crt_call(dev->acpi_handle, &temp);
  if (st != ZX_OK) {
    goto out;
  }
  info->critical_temp_celsius = decikelvin_to_celsius(temp);

  info->max_trip_count = dev->trip_point_count;
  memcpy(info->active_trip, dev->trip_points, sizeof(info->active_trip));

  st = acpi_tmp_call(dev->acpi_handle, &temp);
  if (st != ZX_OK) {
    goto out;
  }
  info->state = 0;
  if (dev->have_trip[0] && (decikelvin_to_celsius(temp) > info->active_trip[0])) {
    info->state |= fuchsia_hardware_thermal_THERMAL_STATE_TRIP_VIOLATION;
  }
out:
  mtx_unlock(&dev->lock);
  return st;
}

static zx_status_t fidl_GetInfo(void* ctx, fidl_txn_t* txn) {
  acpi_thermal_device_t* dev = static_cast<acpi_thermal_device_t*>(ctx);

  // reading state clears the signal
  zx_object_signal(dev->event, ZX_USER_SIGNAL_0, 0);

  fuchsia_hardware_thermal_ThermalInfo info;
  zx_status_t status = acpi_thermal_get_info(dev, &info);
  if (status != ZX_OK) {
    return fuchsia_hardware_thermal_DeviceGetInfo_reply(txn, status, NULL);
  }

  return fuchsia_hardware_thermal_DeviceGetInfo_reply(txn, status, &info);
}

static zx_status_t fidl_GetDeviceInfo(void* ctx, fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceGetDeviceInfo_reply(txn, ZX_ERR_NOT_SUPPORTED, NULL);
}

static zx_status_t fidl_GetDvfsInfo(void* ctx, fuchsia_hardware_thermal_PowerDomain power_domain,
                                    fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceGetDvfsInfo_reply(txn, ZX_ERR_NOT_SUPPORTED, NULL);
}

static zx_status_t fidl_GetTemperatureCelsius(void* ctx, fidl_txn_t* txn) {
  acpi_thermal_device_t* dev = static_cast<acpi_thermal_device_t*>(ctx);
  uint64_t v;

  zx_status_t status = acpi_tmp_call(dev->acpi_handle, &v);
  if (status != ZX_OK) {
    zxlogf(ERROR, "acpi-thermal: acpi error %d in _TMP\n", status);
    return fuchsia_hardware_thermal_DeviceGetTemperatureCelsius_reply(txn, status, 0);
  }

  return fuchsia_hardware_thermal_DeviceGetTemperatureCelsius_reply(txn, ZX_OK,
                                                                    decikelvin_to_celsius(v));
}

static zx_status_t fidl_GetStateChangeEvent(void* ctx, fidl_txn_t* txn) {
  acpi_thermal_device_t* dev = static_cast<acpi_thermal_device_t*>(ctx);

  zx_handle_t handle;
  zx_status_t status = zx_handle_duplicate(dev->event, ZX_RIGHT_SAME_RIGHTS, &handle);

  if (status == ZX_OK) {
    // clear the signal before returning
    zx_object_signal(dev->event, ZX_USER_SIGNAL_0, 0);
  }

  return fuchsia_hardware_thermal_DeviceGetStateChangeEvent_reply(txn, status, handle);
}

static zx_status_t fidl_GetStateChangePort(void* ctx, fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceGetStateChangePort_reply(txn, ZX_ERR_NOT_SUPPORTED,
                                                                 ZX_HANDLE_INVALID);
}

static zx_status_t fidl_SetTripCelsius(void* ctx, uint32_t id, float temp, fidl_txn_t* txn) {
  acpi_thermal_device_t* dev = static_cast<acpi_thermal_device_t*>(ctx);

  if (dev->trip_point_count < 1) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // only one trip point for now
  if (id != 0) {
    return fuchsia_hardware_thermal_DeviceSetTripCelsius_reply(txn, ZX_ERR_INVALID_ARGS);
  }
  ACPI_STATUS acpi_status =
      acpi_evaluate_method_intarg(dev->acpi_handle, "PAT0", celsius_to_decikelvin(temp));
  if (acpi_status != AE_OK) {
    zxlogf(ERROR, "acpi-thermal: acpi error %d in PAT0\n", acpi_status);
    return fuchsia_hardware_thermal_DeviceSetTripCelsius_reply(txn, acpi_to_zx_status(acpi_status));
  }
  mtx_lock(&dev->lock);
  dev->have_trip[0] = true;
  dev->trip_points[0] = temp;
  mtx_unlock(&dev->lock);
  return fuchsia_hardware_thermal_DeviceSetTripCelsius_reply(txn, ZX_OK);
}

static zx_status_t fidl_GetDvfsOperatingPoint(void* ctx,
                                              fuchsia_hardware_thermal_PowerDomain power_domain,
                                              fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceGetDvfsOperatingPoint_reply(txn, ZX_ERR_NOT_SUPPORTED, 0);
}

static zx_status_t fidl_SetDvfsOperatingPoint(void* ctx, uint16_t op_idx,
                                              fuchsia_hardware_thermal_PowerDomain power_domain,
                                              fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceSetDvfsOperatingPoint_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

static zx_status_t fidl_GetFanLevel(void* ctx, fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceGetFanLevel_reply(txn, ZX_ERR_NOT_SUPPORTED, 0);
}

static zx_status_t fidl_SetFanLevel(void* ctx, uint32_t fan_level, fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_DeviceSetFanLevel_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

// Not static so the test harness can use them.
fuchsia_hardware_thermal_Device_ops_t fidl_ops = {
    .GetInfo = fidl_GetInfo,
    .GetDeviceInfo = fidl_GetDeviceInfo,
    .GetDvfsInfo = fidl_GetDvfsInfo,
    .GetTemperatureCelsius = fidl_GetTemperatureCelsius,
    .GetStateChangeEvent = fidl_GetStateChangeEvent,
    .GetStateChangePort = fidl_GetStateChangePort,
    .SetTripCelsius = fidl_SetTripCelsius,
    .GetDvfsOperatingPoint = fidl_GetDvfsOperatingPoint,
    .SetDvfsOperatingPoint = fidl_SetDvfsOperatingPoint,
    .GetFanLevel = fidl_GetFanLevel,
    .SetFanLevel = fidl_SetFanLevel,
};

static zx_status_t acpi_thermal_message(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_thermal_Device_dispatch(ctx, txn, msg, &fidl_ops);
}

static void acpi_thermal_notify(ACPI_HANDLE handle, UINT32 value, void* ctx) {
  acpi_thermal_device_t* dev = static_cast<acpi_thermal_device_t*>(ctx);
  zxlogf(TRACE, "acpi-thermal: got event 0x%x\n", value);
  switch (value) {
    case INT3403_THERMAL_EVENT:
      zx_object_signal(dev->event, 0, ZX_USER_SIGNAL_0);
      break;
  }
}

static void acpi_thermal_release(void* ctx) {
  acpi_thermal_device_t* dev = static_cast<acpi_thermal_device_t*>(ctx);
  AcpiRemoveNotifyHandler(dev->acpi_handle, ACPI_DEVICE_NOTIFY, acpi_thermal_notify);
  zx_handle_close(dev->event);
  free(dev);
}

static zx_protocol_device_t acpi_thermal_device_proto = []() {
  zx_protocol_device_t ops = {};
  ops.version = DEVICE_OPS_VERSION;
  ops.release = acpi_thermal_release;
  ops.message = acpi_thermal_message;
  return ops;
}();

}  // namespace acpi_thermal

zx_status_t thermal_init(zx_device_t* parent, ACPI_DEVICE_INFO* info, ACPI_HANDLE acpi_handle) {
  // only support sensors
  uint64_t type = 0;
  ACPI_STATUS acpi_status = acpi_evaluate_integer(acpi_handle, "PTYP", &type);
  if (acpi_status != AE_OK) {
    zxlogf(ERROR, "acpi-thermal: acpi error %d in PTYP\n", acpi_status);
    return acpi_to_zx_status(acpi_status);
  }
  if (type != INT3403_TYPE_SENSOR) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  acpi_thermal::acpi_thermal_device_t* dev = static_cast<acpi_thermal::acpi_thermal_device_t*>(
      calloc(1, sizeof(acpi_thermal::acpi_thermal_device_t)));
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
                                         acpi_thermal::acpi_thermal_notify, dev);
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

  device_add_args_t args = {};

  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = name;
  args.ctx = dev;
  args.ops = &acpi_thermal::acpi_thermal_device_proto;
  args.proto_id = ZX_PROTOCOL_THERMAL;

  status = device_add(parent, &args, &dev->zxdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "acpi-thermal: could not add device! err=%d\n", status);
    acpi_thermal_release(dev);
    return status;
  }

  zxlogf(TRACE, "acpi-thermal: initialized '%s' %u trip points\n", name, dev->trip_point_count);

  return ZX_OK;
}
