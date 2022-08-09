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

#include "src/camera/bin/usb_device_watcher/device_watcher_impl.h"
#include "src/lib/fsl/io/device_watcher.h"

int main(int argc, char* argv[]) {
  syslog::SetLogSettings({.min_log_level = CAMERA_MIN_LOG_LEVEL},
                         {"camera", "camera_device_watcher"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto context = sys::ComponentContext::Create();

  auto directory = sys::ComponentContext::CreateAndServeOutgoingDirectory();

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

  directory->outgoing()->AddPublicService(server->GetHandler());

  // TODO(ernesthua) - Removed tester interface. Need to restore it on merge back.

  loop.Run();
  return EXIT_SUCCESS;
}
