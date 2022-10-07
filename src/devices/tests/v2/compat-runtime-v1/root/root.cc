// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This header has to come first, and we define our ZX_PROTOCOL, so that
// we don't have to edit protodefs.h to add this test protocol.
#include <bind/fuchsia/compat/cpp/bind.h>
#define ZX_PROTOCOL_PARENT bind_fuchsia_compat::BIND_PROTOCOL_PARENT

#include <fidl/fuchsia.compat.runtime/cpp/driver/fidl.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/driver2/outgoing_directory.h>
#include <lib/fdf/cpp/channel.h>
#include <lib/inspect/cpp/inspect.h>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/intrusive_double_list.h>

#include "src/devices/tests/v2/compat-runtime-v1/root/root-bind.h"

namespace root {

class Root;
using DeviceType = ddk::Device<Root>;
class Root : public DeviceType, public fdf::Server<fuchsia_compat_runtime::Root> {
 public:
  explicit Root(zx_device_t* root)
      : DeviceType(root),
        outgoing_(driver::OutgoingDirectory::Create(fdf::Dispatcher::GetCurrent()->get())) {}
  virtual ~Root() = default;

  static zx_status_t Bind(void* ctx, zx_device_t* dev) {
    auto driver = std::make_unique<Root>(dev);
    zx_status_t status = driver->Bind();
    if (status != ZX_OK) {
      return status;
    }
    // The DriverFramework now owns driver.
    __UNUSED auto ptr = driver.release();
    return ZX_OK;
  }

  zx_status_t Bind() {
    driver::ServiceInstanceHandler handler;
    fuchsia_compat_runtime::Service::Handler service(&handler);

    auto protocol = [this](fdf::ServerEnd<fuchsia_compat_runtime::Root> server_end) {
      fdf::BindServer<fdf::Server<fuchsia_compat_runtime::Root>>(
          fdf::Dispatcher::GetCurrent()->get(), std::move(server_end), this);
    };
    auto status = service.add_root(std::move(protocol));
    if (status.is_error()) {
      return status.status_value();
    }
    status = outgoing_.AddService<fuchsia_compat_runtime::Service>(std::move(handler));
    if (status.is_error()) {
      return status.status_value();
    }
    auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    if (endpoints.is_error()) {
      return endpoints.status_value();
    }
    auto result = outgoing_.Serve(std::move(endpoints->server));
    if (result.is_error()) {
      return result.status_value();
    }

    std::array offers = {
        fuchsia_compat_runtime::Service::Name,
    };

    return DdkAdd(ddk::DeviceAddArgs("root")
                      .set_proto_id(ZX_PROTOCOL_PARENT)
                      .set_runtime_service_offers(offers)
                      .set_outgoing_dir(endpoints->client.TakeChannel()));
  }

  void DdkRelease() { delete this; }

  // fdf::Server<ft::Root>
  void GetString(GetStringCompleter::Sync& completer) override {
    char str[100];
    strcpy(str, "hello world!");
    completer.Reply(std::string(str));
  }

 private:
  driver::OutgoingDirectory outgoing_;
};

static zx_driver_ops_t root_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Root::Bind;
  return ops;
}();

}  // namespace root

ZIRCON_DRIVER(Root, root::root_driver_ops, "zircon", "0.1");
