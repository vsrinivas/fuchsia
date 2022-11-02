// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.transport.test/cpp/driver/wire.h>
#include <fidl/fuchsia.driver.transport.test/cpp/markers.h>
#include <fidl/fuchsia.driver.transport.test/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/driver/component/cpp/outgoing_directory.h>
#include <lib/fdf/cpp/arena.h>
#include <lib/fdf/cpp/channel.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fdf/dispatcher.h>
#include <lib/fidl/cpp/wire/connect_service.h>
#include <lib/fidl/cpp/wire/vector_view.h>
#include <zircon/errors.h>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/empty-protocol.h>

#include "src/devices/tests/driver-transport/parent-driver-bind.h"

namespace fdtt = fuchsia_driver_transport_test;

class Device;
using DeviceType = ddk::Device<Device, ddk::Unbindable, ddk::Messageable<fdtt::TestDevice>::Mixin>;

class Device : public DeviceType,
               public fdf::WireServer<fdtt::DriverTransportProtocol>,
               public ddk::EmptyProtocol<ZX_PROTOCOL_TEST> {
 public:
  static zx_status_t Bind(void* ctx, zx_device_t* device);

  Device(zx_device_t* parent, fdf::UnownedDispatcher dispatcher)
      : DeviceType(parent),
        dispatcher_(std::move(dispatcher)),
        outgoing_(driver::OutgoingDirectory::Create(dispatcher_->get())) {}

  // TestDevice protocol implementation.
  void SetTestData(SetTestDataRequestView request, SetTestDataCompleter::Sync& completer) override;

  // DriverTransportProtocol implementation
  void TransmitData(fdf::Arena& arena, TransmitDataCompleter::Sync& completer) override;

  // Device protocol implementation.
  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

 private:
  fdf::UnownedDispatcher dispatcher_;

  driver::OutgoingDirectory outgoing_;

  // Data set by the test using |SetTestData|.
  uint8_t data_[fdtt::wire::kMaxTransferSize];
  size_t data_size_;
};

// Sets the test data that will be retrieved by |TransmitData|.
void Device::SetTestData(SetTestDataRequestView request, SetTestDataCompleter::Sync& completer) {
  auto ptr = request->in.data();
  data_size_ = request->in.count();
  memcpy(data_, ptr, data_size_);
  completer.ReplySuccess();
}

void Device::TransmitData(fdf::Arena& arena, TransmitDataCompleter::Sync& completer) {
  auto data = fidl::VectorView<uint8_t>::FromExternal(data_, data_size_);
  completer.buffer(std::move(arena)).ReplySuccess(data);
}

// static
zx_status_t Device::Bind(void* ctx, zx_device_t* device) {
  auto dispatcher = fdf::Dispatcher::GetCurrent();
  auto dev = std::make_unique<Device>(device, std::move(dispatcher));

  driver::ServiceInstanceHandler handler;
  fdtt::Service::Handler service(&handler);

  auto protocol =
      [dev = dev.get()](fdf::ServerEnd<fdtt::DriverTransportProtocol> server_end) mutable {
        fdf::BindServer<fdf::WireServer<fdtt::DriverTransportProtocol>>(
            fdf::Dispatcher::GetCurrent()->get(), std::move(server_end), dev);
      };
  auto add_status = service.add_driver_transport_protocol(std::move(protocol));
  if (add_status.is_error()) {
    return add_status.status_value();
  }
  add_status = dev->outgoing_.AddService<fdtt::Service>(std::move(handler));
  if (add_status.is_error()) {
    return add_status.status_value();
  }
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (endpoints.is_error()) {
    return endpoints.status_value();
  }
  auto result = dev->outgoing_.Serve(std::move(endpoints->server));
  if (result.is_error()) {
    return result.status_value();
  }

  std::array offers = {
      fdtt::Service::Name,
  };

  zx_status_t status =
      dev->DdkAdd(ddk::DeviceAddArgs("parent").set_runtime_service_offers(offers).set_outgoing_dir(
          endpoints->client.TakeChannel()));
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

ZIRCON_DRIVER(driver_transport_test_parent, kDriverOps, "zircon", "0.1");
