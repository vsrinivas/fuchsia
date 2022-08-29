// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This header has to come first, and we define our ZX_PROTOCOL, so that
// we don't have to edit protodefs.h to add this test protocol.
#include <bind/fuchsia/compat/cpp/bind.h>
#define ZX_PROTOCOL_PARENT bind_fuchsia_compat::BIND_PROTOCOL_PARENT

#include <fidl/fuchsia.compat.runtime/cpp/driver/fidl.h>
#include <fidl/fuchsia.compat.runtime/cpp/fidl.h>
#include <lib/fdf/cpp/channel.h>
#include <lib/inspect/cpp/inspect.h>

#include <ddktl/device.h>

#include "src/devices/tests/v2/compat-runtime-v1/leaf/leaf-bind.h"

namespace leaf {

class Leaf;
using DeviceType =
    ddk::Device<Leaf, ddk::Initializable, ddk::Messageable<fuchsia_compat_runtime::Leaf>::Mixin>;
class Leaf : public DeviceType {
 public:
  explicit Leaf(zx_device_t* root) : DeviceType(root) {}
  virtual ~Leaf() = default;

  static zx_status_t Bind(void* ctx, zx_device_t* dev) {
    auto driver = std::make_unique<Leaf>(dev);
    zx_status_t status = driver->Bind();
    if (status != ZX_OK) {
      return status;
    }
    // The DriverFramework now owns driver.
    __UNUSED auto ptr = driver.release();
    return ZX_OK;
  }

  zx_status_t Bind() { return DdkAdd(ddk::DeviceAddArgs("leaf")); }

  void DdkInit(ddk::InitTxn txn) {
    auto endpoints = fdf::CreateEndpoints<fuchsia_compat_runtime::Root>();
    if (endpoints.is_error()) {
      return txn.Reply(endpoints.status_value());
    }

    auto channels = fdf::ChannelPair::Create(0);
    if (channels.is_error()) {
      return txn.Reply(channels.status_value());
    }
    // Connect to our parent driver.
    zx_status_t status =
        DdkServiceConnect(fidl::DiscoverableProtocolName<fuchsia_compat_runtime::Root>,
                          endpoints->server.TakeHandle());
    if (status != ZX_OK) {
      return txn.Reply(status);
    }
    root_client_ = fdf::Client<fuchsia_compat_runtime::Root>(std::move(endpoints->client),
                                                             fdf::Dispatcher::GetCurrent()->get());

    txn.Reply(ZX_OK);
  }

  void DdkRelease() { delete this; }

  // |fuchsia_compat_runtime::Leaf| implementation.
  void GetString(GetStringRequestView request, GetStringCompleter::Sync& completer) override {
    root_client_->GetString().ThenExactlyOnce(
        [completer = completer.ToAsync()](
            fdf::Result<fuchsia_compat_runtime::Root::GetString>& result) mutable {
          ZX_ASSERT(result.is_ok());
          completer.Reply(fidl::StringView::FromExternal(result->response()));
        });
  }

 private:
  fdf::Client<fuchsia_compat_runtime::Root> root_client_;
};

static zx_driver_ops_t leaf_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Leaf::Bind;
  return ops;
}();

}  // namespace leaf

ZIRCON_DRIVER(Leaf, leaf::leaf_driver_ops, "zircon", "0.1");
