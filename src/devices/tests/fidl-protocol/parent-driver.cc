// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.examples.echo/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async/dispatcher.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/fidl/llcpp/connect_service.h>
#include <lib/svc/outgoing.h>
#include <lib/zx/channel.h>

#include <string>

#include <ddktl/device.h>
#include <ddktl/unbind-txn.h>
#include <fbl/ref_ptr.h>

#include "lib/fidl/llcpp/channel.h"
#include "src/devices/tests/fidl-protocol/parent-driver-bind.h"

namespace {

class Device;
using DeviceParent = ddk::Device<Device, ddk::Unbindable>;

class Device : public DeviceParent, public fidl::WireServer<fidl_examples_echo::Echo> {
 public:
  static zx_status_t Bind(void* ctx, zx_device_t* parent) {
    auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    if (endpoints.is_error()) {
      return endpoints.status_value();
    }

    auto device = std::make_unique<Device>(parent, device_get_dispatcher(parent));

    device->outgoing_dir_.svc_dir()->AddEntry(
        fidl::DiscoverableProtocolName<fidl_examples_echo::Echo>,
        fbl::MakeRefCounted<fs::Service>(
            [device = device.get()](fidl::ServerEnd<fidl_examples_echo::Echo> request) mutable {
              device->Bind(std::move(request));
              return ZX_OK;
            }));

    auto status = device->outgoing_dir_.Serve(std::move(endpoints->server));
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to service the outoing directory");
      return status;
    }

    std::array offers = {
        fidl::DiscoverableProtocolName<fidl_examples_echo::Echo>,
    };

    status = device->DdkAdd(ddk::DeviceAddArgs("parent")
                                .set_flags(DEVICE_ADD_MUST_ISOLATE)
                                .set_fidl_protocol_offers(offers)
                                .set_outgoing_dir(endpoints->client.TakeChannel()));
    if (status == ZX_OK) {
      __UNUSED auto ptr = device.release();
    } else {
      zxlogf(ERROR, "Failed to add device");
    }

    return status;
  }

  Device(zx_device_t* parent, async_dispatcher_t* dispatcher)
      : DeviceParent(parent), outgoing_dir_(dispatcher) {}

  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

 private:
  void Bind(fidl::ServerEnd<fidl_examples_echo::Echo> request) {
    fidl::BindServer<fidl::WireServer<fidl_examples_echo::Echo>>(device_get_dispatcher(parent()),
                                                                 std::move(request), this);
  }
  void EchoString(EchoStringRequestView request, EchoStringCompleter::Sync& completer) override {
    completer.Reply(request->value);
  }

  svc::Outgoing outgoing_dir_;
};

static constexpr zx_driver_ops_t kDriverOps = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Device::Bind;
  return ops;
}();

}  // namespace

ZIRCON_DRIVER(fidl_protocol_test_parent, kDriverOps, "zircon", "0.1");
