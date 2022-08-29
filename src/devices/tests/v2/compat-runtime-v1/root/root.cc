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
#include <lib/fdf/cpp/channel.h>
#include <lib/inspect/cpp/inspect.h>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/intrusive_double_list.h>

#include "src/devices/tests/v2/compat-runtime-v1/root/root-bind.h"

namespace root {

class Root;
using DeviceType = ddk::Device<Root, ddk::Initializable, ddk::ServiceConnectable>;
class Root : public DeviceType, public fdf::Server<fuchsia_compat_runtime::Root> {
 public:
  explicit Root(zx_device_t* root) : DeviceType(root) {}
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

  zx_status_t Bind() { return DdkAdd(ddk::DeviceAddArgs("root").set_proto_id(ZX_PROTOCOL_PARENT)); }

  void DdkInit(ddk::InitTxn txn) { txn.Reply(ZX_OK); }

  void DdkRelease() { delete this; }

  zx_status_t DdkServiceConnect(const char* service_name, fdf::Channel channel) {
    if (std::string_view(service_name) !=
        fidl::DiscoverableProtocolName<fuchsia_compat_runtime::Root>) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    fdf::ServerEnd<fuchsia_compat_runtime::Root> server_end(std::move(channel));
    fdf::BindServer<fdf::Server<fuchsia_compat_runtime::Root>>(fdf::Dispatcher::GetCurrent()->get(),
                                                               std::move(server_end), this);

    return ZX_OK;
  }

  // fdf::Server<ft::Root>
  void GetString(GetStringRequest& request, GetStringCompleter::Sync& completer) override {
    char str[100];
    strcpy(str, "hello world!");
    completer.Reply(std::string(str));
  }
};

static zx_driver_ops_t root_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Root::Bind;
  return ops;
}();

}  // namespace root

ZIRCON_DRIVER(Root, root::root_driver_ops, "zircon", "0.1");
