// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/camera/test/cpp/fidl.h>
#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/hardware/camera/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/logger.h>

#include "src/camera/bin/device_watcher/device_watcher_impl.h"
#include "src/lib/fsl/io/device_watcher.h"

constexpr auto kCameraPath = "/dev/class/camera";

static fit::result<fuchsia::hardware::camera::DeviceHandle, zx_status_t> GetCamera(
    std::string path) {
  fuchsia::hardware::camera::DeviceHandle camera;
  zx_status_t status = fdio_service_connect(path.c_str(), camera.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  return fit::ok(std::move(camera));
}

class DeviceWatcherTesterImpl : public fuchsia::camera::test::DeviceWatcherTester {
 public:
  using InjectDeviceCallback = fit::function<void(fuchsia::hardware::camera::DeviceHandle)>;
  DeviceWatcherTesterImpl(InjectDeviceCallback callback) : callback_(std::move(callback)) {}
  fidl::InterfaceRequestHandler<fuchsia::camera::test::DeviceWatcherTester> GetHandler() {
    return fit::bind_member(this, &DeviceWatcherTesterImpl::OnNewRequest);
  }

 private:
  void OnNewRequest(fidl::InterfaceRequest<fuchsia::camera::test::DeviceWatcherTester> request) {
    bindings_.AddBinding(this, std::move(request));
  }

  // |fuchsia::camera::test::DeviceWatcherTester|
  void InjectDevice(fuchsia::hardware::camera::DeviceHandle camera) override {
    callback_(std::move(camera));
  }

  fidl::BindingSet<fuchsia::camera::test::DeviceWatcherTester> bindings_;
  InjectDeviceCallback callback_;
};

int main(int argc, char* argv[]) {
  syslog::InitLogger({"camera", "camera_device_watcher"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();

  fuchsia::sys::LauncherHandle launcher;
  zx_status_t status = context->svc()->Connect(launcher.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "Failed to connect to launcher service.";
    return EXIT_FAILURE;
  }

  auto result = DeviceWatcherImpl::Create(std::move(launcher));
  if (result.is_error()) {
    FX_PLOGS(FATAL, result.error());
    return EXIT_FAILURE;
  }

  auto server = result.take_value();

  auto watcher = fsl::DeviceWatcher::CreateWithIdleCallback(
      kCameraPath,
      [&](int dir_fd, std::string path) {
        auto full_path = std::string(kCameraPath) + "/" + path;
        auto result = GetCamera(full_path);
        if (result.is_error()) {
          FX_PLOGS(INFO, result.error()) << "Couldn't get camera from " << full_path
                                         << ". This device will not be exposed to clients.";
          return;
        }
        auto add_result = server->AddDevice(result.take_value());
        if (add_result.is_error()) {
          FX_PLOGS(WARNING, add_result.error()) << "Failed to add camera from " << full_path
                                                << ". This device will not be exposed to clients.";
          return;
        }
      },
      [&]() { server->UpdateClients(); });
  if (!watcher) {
    FX_LOGS(FATAL);
    return EXIT_FAILURE;
  }

  context->outgoing()->AddPublicService(server->GetHandler());

  DeviceWatcherTesterImpl tester([&](fuchsia::hardware::camera::DeviceHandle device) {
    auto result = server->AddDevice(std::move(device));
    if (result.is_error()) {
      FX_PLOGS(ERROR, result.error()) << "Failed to add test device.";
      return;
    }
    server->UpdateClients();
  });

  context->outgoing()->AddPublicService(tester.GetHandler());

  // Run should never return.
  status = loop.Run();
  FX_PLOGS(FATAL, status) << "Loop exited unexpectedly.";
  return EXIT_FAILURE;
}
