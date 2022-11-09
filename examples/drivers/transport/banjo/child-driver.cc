// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/examples/gizmo/cpp/banjo.h>
#include <lib/ddk/device.h>

#include <ddktl/device.h>

#include "examples/drivers/transport/banjo/child-driver-bind.h"

namespace banjo_transport {

class BanjoClientDevice;
using DeviceType = ddk::Device<BanjoClientDevice, ddk::Initializable>;

class BanjoClientDevice : public DeviceType {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  explicit BanjoClientDevice(zx_device_t* parent) : DeviceType(parent) {}
  virtual ~BanjoClientDevice() = default;

  zx_status_t Bind(ddk::MiscProtocolClient misc);

  // Device protocol implementation.
  void DdkInit(ddk::InitTxn txn) { txn.Reply(ZX_OK); }
  void DdkRelease() { delete this; }
};

// static
zx_status_t BanjoClientDevice::Create(void* ctx, zx_device_t* parent) {
  misc_protocol_t misc;
  zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_MISC, &misc);
  if (status != ZX_OK) {
    return status;
  }
  auto device = std::make_unique<banjo_transport::BanjoClientDevice>(parent);

  status = device->Bind(&misc);
  if (status == ZX_OK) {
    // Driver framework now owns device.
    __UNUSED auto* dev = device.release();
  }
  return status;
}

zx_status_t BanjoClientDevice::Bind(ddk::MiscProtocolClient misc) {
  uint32_t response;
  zx_status_t status = misc.GetHardwareId(&response);
  if (status != ZX_OK) {
    return status;
  }
  zxlogf(INFO, "Transport client hardware: %X", response);

  uint32_t major_version;
  uint32_t minor_version;
  status = misc.GetFirmwareVersion(&major_version, &minor_version);
  if (status != ZX_OK) {
    return status;
  }
  zxlogf(INFO, "Transport client firmware: %d.%d", major_version, minor_version);

  return DdkAdd(ddk::DeviceAddArgs("test").set_flags(DEVICE_ADD_NON_BINDABLE));
}

}  // namespace banjo_transport

static zx_driver_ops_t banjo_client_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = banjo_transport::BanjoClientDevice::Create;
  return ops;
}();

ZIRCON_DRIVER(BanjoClientDevice, banjo_client_driver_ops, "zircon", "0.1");
