// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.compat.runtime.test/cpp/driver/fidl.h>
#include <fidl/fuchsia.compat.runtime.test/cpp/fidl.h>
#include <lib/fdf/cpp/channel.h>
#include <lib/fdf/cpp/dispatcher.h>

#include <bind/fuchsia/test/cpp/bind.h>
#include <ddktl/device.h>

#include "src/devices/tests/v2/compat-runtime/v1.bind.h"

namespace ft = fuchsia_compat_runtime_test;

namespace v1 {

class V1;
using DeviceType = ddk::Device<V1, ddk::Messageable<ft::Leaf>::Mixin>;
class V1 : public DeviceType {
 public:
  explicit V1(zx_device_t* root) : DeviceType(root) {}
  virtual ~V1() = default;

  static zx_status_t Bind(void* ctx, zx_device_t* dev) {
    zxlogf(INFO, "v1_bind");

    auto driver = std::make_unique<V1>(dev);
    zx_status_t status = driver->Bind();
    if (status != ZX_OK) {
      return status;
    }
    // The DriverFramework now owns driver.
    __UNUSED auto ptr = driver.release();
    return ZX_OK;
  }

  void DdkRelease() { delete this; }

  // fidl::Server<ft::Leaf>
  void GetString(GetStringCompleter::Sync& completer) override {
    client_->GetString().ThenExactlyOnce(
        [completer = completer.ToAsync()](fdf::Result<ft::Root::GetString>& result) mutable {
          ZX_ASSERT(result.is_ok());
          completer.Reply(fidl::StringView::FromExternal(result->response()));
        });
  }

 private:
  zx_status_t Bind() {
    zx_status_t status = ConnectToRootRuntimeProtocol();
    if (status != ZX_OK) {
      return status;
    }
    return DdkAdd(ddk::DeviceAddArgs("leaf"));
  }

  zx_status_t ConnectToRootRuntimeProtocol() {
    auto client_end = DdkConnectRuntimeProtocol<ft::Service::Root>();
    if (client_end.is_error()) {
      return client_end.status_value();
    }
    client_.Bind(std::move(*client_end), fdf::Dispatcher::GetCurrent()->get());
    return ZX_OK;
  }

 private:
  fdf::Client<ft::Root> client_;
};

static zx_driver_ops_t driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = V1::Bind;
  return ops;
}();

}  // namespace v1

ZIRCON_DRIVER(V1, v1::driver_ops, "zircon", "0.1");
