// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/result.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>

class DeviceWatcherImpl : public fuchsia::camera3::DeviceWatcher {
 public:
  static fit::result<std::unique_ptr<DeviceWatcherImpl>, zx_status_t> Create(
      const sys::ComponentContext* context) {
    auto watcher = std::make_unique<DeviceWatcherImpl>();

    zx_status_t status =
        context->outgoing()->AddPublicService(watcher->bindings_.GetHandler(watcher.get()));
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status);
      return fit::error(status);
    }

    return fit::ok(std::move(watcher));
  }

 private:
  void WatchDevices(WatchDevicesCallback callback) override {
    FX_PLOGS(WARNING, ZX_ERR_NOT_SUPPORTED);
    callback({});
  }

  void ConnectToDevice(uint64_t id,
                       fidl::InterfaceRequest<fuchsia::camera3::Device> request) override {
    FX_PLOGS(WARNING, ZX_ERR_NOT_SUPPORTED);
    request.Close(ZX_ERR_NOT_SUPPORTED);
  }

  fidl::BindingSet<fuchsia::camera3::DeviceWatcher> bindings_;
};

int main(int argc, char* argv[]) {
  syslog::InitLogger({"camera_device_watcher"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();

  auto result = DeviceWatcherImpl::Create(context.get());
  if (result.is_error()) {
    FX_PLOGS(ERROR, result.error());
    return EXIT_FAILURE;
  }

  auto watcher = result.take_value();

  zx_status_t status = loop.Run();
  FX_PLOGS(ERROR, status) << "Loop exited unexpectedly.";

  return EXIT_FAILURE;
}
