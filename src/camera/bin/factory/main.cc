// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/syslog/global.h>

#include "src/camera/bin/factory/factory_server.h"

int main() {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  // Connect to required services.
  fuchsia::sysmem::AllocatorHandle allocator;
  zx_status_t status = context->svc()->Connect(allocator.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to request Allocator service.";
    return EXIT_FAILURE;
  }

  fuchsia::camera3::DeviceWatcherHandle watcher;
  status = context->svc()->Connect(watcher.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to request DeviceWatcher service.";
    return EXIT_FAILURE;
  }

  // Create the factory server.
  auto factory_server_result =
      camera::FactoryServer::Create(std::move(allocator), std::move(watcher), [&] { loop.Quit(); });
  if (factory_server_result.is_error()) {
    FX_PLOGS(ERROR, factory_server_result.error()) << "Failed to create FactoryServer.";
    return EXIT_FAILURE;
  }
  auto factory_server = factory_server_result.take_value();

  // Publish the camera-factory service.
  context->outgoing()->AddPublicService(factory_server->GetHandler());

  loop.Run();

  factory_server = nullptr;
  return EXIT_SUCCESS;
}
