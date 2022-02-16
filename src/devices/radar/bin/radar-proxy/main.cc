// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>

#include "radar-proxy.h"
#include "src/lib/fsl/io/device_watcher.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  radar::RadarProxy proxy;

  // Calls DeviceAdded() for each existing device during construction.
  auto watcher = fsl::DeviceWatcher::Create(
      radar::RadarProxy::kRadarDeviceDirectory,
      [&](int dir_fd, const std::string& filename) { proxy.DeviceAdded(dir_fd, filename); });
  if (!watcher) {
    return EXIT_FAILURE;
  }

  fidl::Binding<fuchsia::hardware::radar::RadarBurstReaderProvider> binding(&proxy);
  fidl::InterfaceRequestHandler<fuchsia::hardware::radar::RadarBurstReaderProvider> handler =
      [&](fidl::InterfaceRequest<fuchsia::hardware::radar::RadarBurstReaderProvider> request) {
        binding.Bind(std::move(request));
      };
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  context->outgoing()->AddPublicService(std::move(handler));

  return loop.Run();
}
