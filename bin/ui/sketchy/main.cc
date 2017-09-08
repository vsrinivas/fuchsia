// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trace-provider/provider.h>

#include "application/lib/app/application_context.h"
#include "apps/mozart/src/sketchy/app.h"
#include "escher/escher.h"

int main(int argc, const char** argv) {
  // Only enable Vulkan validation layers when in debug mode.
  escher::VulkanInstance::Params instance_params(
      {{}, {VK_EXT_DEBUG_REPORT_EXTENSION_NAME}, false});
#if !defined(NDEBUG)
  instance_params.layer_names.insert("VK_LAYER_LUNARG_standard_validation");
#endif
  auto vulkan_instance =
      escher::VulkanInstance::New(std::move(instance_params));

  auto vulkan_device = escher::VulkanDeviceQueues::New(
      vulkan_instance,
      {{VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME}, vk::SurfaceKHR()});

  escher::Escher escher(vulkan_device);

  mtl::MessageLoop loop;
  trace::TraceProvider trace_provider(loop.async());

  sketchy_service::App app(&escher);
  loop.Run();

  return 0;
}
