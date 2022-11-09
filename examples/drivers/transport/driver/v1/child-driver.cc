// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.examples.gizmo/cpp/driver/wire.h>
#include <lib/ddk/device.h>

#include <ddktl/device.h>

#include "examples/drivers/transport/driver/v1/child-driver-bind.h"

namespace driver_transport {

class DriverClientDevice;
using DeviceType = ddk::Device<DriverClientDevice, ddk::Initializable>;

class DriverClientDevice : public DeviceType {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  explicit DriverClientDevice(zx_device_t* parent) : DeviceType(parent) {}
  virtual ~DriverClientDevice() = default;

  zx_status_t Bind();

  // Device protocol implementation.
  void DdkInit(ddk::InitTxn txn) { txn.Reply(ZX_OK); }
  void DdkRelease() { delete this; }
};

// static
zx_status_t DriverClientDevice::Create(void* ctx, zx_device_t* parent) {
  auto device = std::make_unique<driver_transport::DriverClientDevice>(parent);

  auto status = device->Bind();
  if (status == ZX_OK) {
    // Driver framework now owns device.
    __UNUSED auto* dev = device.release();
  }
  return status;
}

zx_status_t DriverClientDevice::Bind() {
  auto client_end = DdkConnectRuntimeProtocol<fuchsia_examples_gizmo::Service::Device>();
  if (client_end.is_error()) {
    zxlogf(ERROR, "Failed to connect fidl protocol");
    return client_end.status_value();
  }
  auto client = fdf::WireSyncClient(std::move(*client_end));
  fdf::Arena arena('EXAM');

  auto hardware_result = client.buffer(arena)->GetHardwareId();
  if (!hardware_result.ok()) {
    zxlogf(ERROR, "Failed to request hardware ID: %s", hardware_result.status_string());
    return hardware_result.status();
  } else if (hardware_result->is_error()) {
    zxlogf(ERROR, "Hardware ID request returned an error: %d", hardware_result->error_value());
    return hardware_result->error_value();
  }
  zxlogf(INFO, "Transport client hardware: %X", hardware_result.value().value()->response);

  auto firmware_result = client.buffer(arena)->GetFirmwareVersion();
  if (!firmware_result.ok()) {
    zxlogf(ERROR, "Failed to request firmware version: %s", firmware_result.status_string());
    return firmware_result.status();
  } else if (firmware_result->is_error()) {
    zxlogf(ERROR, "Firmware version request returned an error: %d", firmware_result->error_value());
    return firmware_result->error_value();
  }
  zxlogf(INFO, "Transport client firmware: %d.%d", firmware_result.value().value()->major,
         firmware_result.value().value()->minor);

  return DdkAdd(ddk::DeviceAddArgs("test").set_flags(DEVICE_ADD_NON_BINDABLE));
}

}  // namespace driver_transport

static zx_driver_ops_t driver_client_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = driver_transport::DriverClientDevice::Create;
  return ops;
}();

ZIRCON_DRIVER(DriverClientDevice, driver_client_driver_ops, "zircon", "0.1");
