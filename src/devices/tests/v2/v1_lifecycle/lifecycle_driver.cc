// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.device.fs/cpp/fidl.h>
#include <fidl/fuchsia.driver.compat/cpp/fidl.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <fidl/fuchsia.lifecycle.test/cpp/wire.h>
#include <lib/driver2/driver2_cpp.h>
#include <lib/driver_compat/context.h>
#include <lib/driver_compat/device_server.h>

namespace fdf {
using namespace fuchsia_driver_framework;
}  // namespace fdf

namespace ft = fuchsia_lifecycle_test;

namespace {

class LifecycleDriver : public driver::DriverBase, public fidl::WireServer<ft::Device> {
 public:
  LifecycleDriver(driver::DriverStartArgs start_args, fdf::UnownedDispatcher driver_dispatcher)
      : DriverBase("lifeycle-driver", std::move(start_args), std::move(driver_dispatcher)) {}

  zx::status<> Start() override {
    FDF_LOG(INFO, "Starting lifecycle driver");

    // Serve our Service.
    driver::ServiceInstanceHandler handler;
    ft::Service::Handler service(&handler);

    auto result = service.add_device([this](fidl::ServerEnd<ft::Device> request) -> void {
      fidl::BindServer(dispatcher(), std::move(request), this);
    });
    ZX_ASSERT(result.is_ok());

    result = context().outgoing()->AddService<ft::Service>(std::move(handler));
    if (result.is_error()) {
      FDF_SLOG(ERROR, "Failed to add Demo service", KV("status", result.status_string()));
      return result.take_error();
    }

    // Create our compat context, and serve our device when it's created.
    compat::Context::ConnectAndCreate(
        &context(), dispatcher(), [this](zx::status<std::shared_ptr<compat::Context>> context) {
          if (!context.is_ok()) {
            FDF_LOG(ERROR, "Call to Context::ConnectAndCreate failed: %s", context.status_string());
            node().reset();
            return;
          }
          compat_context_ = std::move(*context);
          const auto kDeviceName = "lifecycle-device";
          child_ = compat::DeviceServer(
              kDeviceName, 0, compat_context_->TopologicalPath(kDeviceName), compat::MetadataMap());
          const auto kServicePath =
              std::string(ft::Service::Name) + "/" + component::kDefaultInstance + "/device";
          child_->ExportToDevfs(
              compat_context_->devfs_exporter(), kServicePath, [this](zx_status_t status) {
                if (status != ZX_OK) {
                  FDF_LOG(WARNING, "Failed to export to devfs: %s", zx_status_get_string(status));
                  node().reset();
                }
              });
        });
    return zx::ok();
  }

  // fidl::WireServer<ft::Device>
  void Ping(PingCompleter::Sync& completer) override { completer.Reply(); }

 private:
  std::optional<compat::DeviceServer> child_;
  std::shared_ptr<compat::Context> compat_context_;
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V2(driver::Record<LifecycleDriver>);
