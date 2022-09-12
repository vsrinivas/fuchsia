// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/camera/test/cpp/fidl.h>
#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/hardware/camera/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include "src/camera/bin/device_watcher/device_watcher_impl.h"
#include "src/lib/fsl/io/device_watcher.h"

class DeviceWatcherTesterImpl : public fuchsia::camera::test::DeviceWatcherTester {
 public:
  using InjectDeviceCallback = fit::function<void(fuchsia::hardware::camera::DeviceHandle)>;
  using InjectDeviceByPathCallback = fit::function<void(std::string)>;
  explicit DeviceWatcherTesterImpl(InjectDeviceCallback callback)
      : callback_(std::move(callback)) {}
  explicit DeviceWatcherTesterImpl(InjectDeviceByPathCallback callback)
      : by_path_callback_(std::move(callback)) {}
  fidl::InterfaceRequestHandler<fuchsia::camera::test::DeviceWatcherTester> GetHandler() {
    return fit::bind_member(this, &DeviceWatcherTesterImpl::OnNewRequest);
  }

 private:
  void OnNewRequest(fidl::InterfaceRequest<fuchsia::camera::test::DeviceWatcherTester> request) {
    bindings_.AddBinding(this, std::move(request));
  }

  // |fuchsia::camera::test::DeviceWatcherTester|
  void InjectDevice(fuchsia::hardware::camera::DeviceHandle camera) override {
    ZX_ASSERT(callback_);
    callback_(std::move(camera));
  }
  void InjectDeviceByPath(std::string path) override {
    ZX_ASSERT(by_path_callback_);
    by_path_callback_(std::move(path));
  }

  fidl::BindingSet<fuchsia::camera::test::DeviceWatcherTester> bindings_;
  InjectDeviceCallback callback_ = nullptr;
  InjectDeviceByPathCallback by_path_callback_ = nullptr;
};

int main(int argc, char* argv[]) {
  syslog::SetLogSettings({.min_log_level = CAMERA_MIN_LOG_LEVEL},
                         {"camera", "camera_device_watcher"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto context = sys::ComponentContext::Create();

  fuchsia::component::RealmHandle realm;
  zx_status_t status = context->svc()->Connect(realm.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "Failed to connect to realm service.";
    return EXIT_FAILURE;
  }

  auto result =
      camera::DeviceWatcherImpl::Create(std::move(context), std::move(realm), loop.dispatcher());
  if (result.is_error()) {
    FX_PLOGS(FATAL, result.error());
    return EXIT_FAILURE;
  }

  auto server = result.take_value();
  auto watcher = fsl::DeviceWatcher::CreateWithIdleCallback(
      camera::kCameraPath, [&](int dir_fd, std::string path) { server->AddDeviceByPath(path); },
      [&]() { server->UpdateClients(); });
  if (!watcher) {
    FX_LOGS(FATAL) << "Failed to create fsl::DeviceWatcher";
    return EXIT_FAILURE;
  }

  // TODO(ernesthua) - Probably should move this within DeviceWatcherImpl to avoid having to create
  // two sys::ComponentContext's.
  auto directory = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  directory->outgoing()->AddPublicService(server->GetHandler());

  DeviceWatcherTesterImpl tester([&](std::string path) {
    server->AddDeviceByPath(std::move(path));
    server->UpdateClients();
  });

  directory->outgoing()->AddPublicService(tester.GetHandler());

  loop.Run();
  return EXIT_SUCCESS;
}
