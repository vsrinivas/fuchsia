// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/examples/gizmo/cpp/banjo.h>
#include <lib/ddk/device.h>

#include <ddktl/device.h>

#include "examples/drivers/transport/banjo/parent-driver-bind.h"

namespace banjo_transport {

class BanjoTransportDevice;
using DeviceType = ddk::Device<BanjoTransportDevice, ddk::Initializable>;

class BanjoTransportDevice : public DeviceType,
                             public ddk::MiscProtocol<BanjoTransportDevice, ddk::base_protocol> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  explicit BanjoTransportDevice(zx_device_t* parent) : DeviceType(parent) {}
  virtual ~BanjoTransportDevice() = default;

  zx_status_t Bind();

  zx_status_t MiscGetHardwareId(uint32_t* out_response);
  zx_status_t MiscGetFirmwareVersion(uint32_t* out_major, uint32_t* out_minor);

  // Device protocol implementation.
  void DdkInit(ddk::InitTxn txn) { txn.Reply(ZX_OK); }
  void DdkRelease() { delete this; }
};

// static
zx_status_t BanjoTransportDevice::Create(void* ctx, zx_device_t* parent) {
  auto device = std::make_unique<banjo_transport::BanjoTransportDevice>(parent);

  zx_status_t status = device->Bind();
  if (status == ZX_OK) {
    // Driver framework now owns device.
    __UNUSED auto* dev = device.release();
  }
  return status;
}

zx_status_t BanjoTransportDevice::Bind() {
  return DdkAdd(ddk::DeviceAddArgs("transport-child").set_proto_id(ZX_PROTOCOL_MISC));
}

zx_status_t BanjoTransportDevice::MiscGetHardwareId(uint32_t* out_response) {
  *out_response = 0x1234ABCD;

  return ZX_OK;
}

zx_status_t BanjoTransportDevice::MiscGetFirmwareVersion(uint32_t* out_major, uint32_t* out_minor) {
  *out_major = 0x0;
  *out_minor = 0x1;

  return ZX_OK;
}

}  // namespace banjo_transport

static zx_driver_ops_t banjo_transport_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = banjo_transport::BanjoTransportDevice::Create;
  return ops;
}();

ZIRCON_DRIVER(BanjoTransportDevice, banjo_transport_driver_ops, "zircon", "0.1");
