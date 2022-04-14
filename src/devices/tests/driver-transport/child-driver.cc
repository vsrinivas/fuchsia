// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.transport.test/cpp/driver/wire.h>
#include <fidl/fuchsia.driver.transport.test/cpp/markers.h>
#include <fidl/fuchsia.driver.transport.test/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/fdf/dispatcher.h>
#include <lib/fidl/llcpp/arena.h>
#include <lib/fidl/llcpp/connect_service.h>
#include <lib/fidl_driver/cpp/wire_messaging_declarations.h>

#include <ddktl/device.h>

#include "src/devices/tests/driver-transport/child-driver-bind.h"

namespace fdtt = fuchsia_driver_transport_test;

class Device;
using DeviceType =
    ddk::Device<Device, ddk::Unbindable, ddk::Messageable<fdtt::TestDeviceChild>::Mixin>;

class Device : public DeviceType {
 public:
  static zx_status_t Bind(void* ctx, zx_device_t* device);

  Device(zx_device_t* parent, fdf::ClientEnd<fdtt::DriverTransportProtocol> client,
         fdf_dispatcher_t* dispatcher)
      : DeviceType(parent), client_(std::move(client), dispatcher) {}

  // fdtt::TestDeviceChild protocol implementation.
  void GetParentDataOverDriverTransport(
      GetParentDataOverDriverTransportRequestView request,
      GetParentDataOverDriverTransportCompleter::Sync& completer) override;

  // Device protocol implementation.
  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

 private:
  fdf::WireSharedClient<fdtt::DriverTransportProtocol> client_;
};

void Device::GetParentDataOverDriverTransport(
    GetParentDataOverDriverTransportRequestView request,
    GetParentDataOverDriverTransportCompleter::Sync& completer) {
  std::string_view tag{""};
  auto arena = fdf::Arena::Create(0, tag);
  if (arena.is_error()) {
    completer.ReplyError(arena.status_value());
    return;
  }

  // Send a request to the parent driver over the driver transport.
  client_.buffer(*std::move(arena))
      ->TransmitData()
      .ThenExactlyOnce(
          [completer = completer.ToAsync()](
              fdf::WireUnownedResult<fdtt::DriverTransportProtocol::TransmitData>& result) mutable {
            if (!result.ok()) {
              zxlogf(ERROR, "%s", result.FormatDescription().c_str());
              completer.ReplyError(result.status());
              return;
            }
            if (result->result.is_err()) {
              zxlogf(ERROR, "TransmitData failed with status: %d", result->result.err());
              completer.ReplyError(result->result.err());
              return;
            }

            // Reply to the test's fidl request with the data.
            completer.ReplySuccess(result->result.response().out);
          });
}

// static
zx_status_t Device::Bind(void* ctx, zx_device_t* device) {
  auto endpoints = fdf::CreateEndpoints<fdtt::DriverTransportProtocol>();
  if (endpoints.is_error()) {
    return endpoints.status_value();
  }

  auto* dispatcher = fdf_dispatcher_get_current_dispatcher();
  auto dev = std::make_unique<Device>(device, std::move(endpoints->client), dispatcher);
  // Connect to our parent driver.
  zx_status_t status =
      dev->DdkServiceConnect(fidl::DiscoverableProtocolName<fdtt::DriverTransportProtocol>,
                             endpoints->server.TakeHandle());
  if (status != ZX_OK) {
    zxlogf(ERROR, "DdkServiceConnect Failed =(");
    return status;
  }

  status = dev->DdkAdd("child");
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

static zx_driver_ops_t kDriverOps = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Device::Bind;
  return ops;
}();

ZIRCON_DRIVER(driver_transport_test_child, kDriverOps, "zircon", "0.1");
