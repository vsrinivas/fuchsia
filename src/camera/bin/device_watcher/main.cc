// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/logger.h>

#include "src/camera/bin/device_watcher/device_watcher_impl.h"
#include "src/lib/fsl/io/device_watcher.h"

constexpr auto kCameraPath = "/dev/class/camera";

std::string FullPath(std::string filename) { return std::string(kCameraPath) + "/" + filename; }

int main(int argc, char* argv[]) {
  syslog::InitLogger({"camera", "camera_device_watcher"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();

  auto result = DeviceWatcherImpl::Create();
  if (result.is_error()) {
    FX_PLOGS(FATAL, result.error());
    return EXIT_FAILURE;
  }

  auto server = result.take_value();

  auto watcher = fsl::DeviceWatcher::CreateWithIdleCallback(
      kCameraPath, [&](int dir_fd, std::string path) { server->AddDevice(FullPath(path)); },
      [&]() { server->UpdateClients(); });
  if (!watcher) {
    FX_LOGS(FATAL);
    return EXIT_FAILURE;
  }

  context->outgoing()->AddPublicService(server->GetHandler());

  // Run should never return.
  zx_status_t status = loop.Run();
  FX_PLOGS(FATAL, status) << "Loop exited unexpectedly.";
  return EXIT_FAILURE;
}
