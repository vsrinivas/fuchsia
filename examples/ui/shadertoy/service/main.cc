// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <trace-provider/provider.h>

#include "garnet/examples/ui/shadertoy/service/app.h"
#include "lib/app/cpp/startup_context.h"
#include "lib/escher/escher.h"
#include "lib/escher/escher_process_init.h"
#include "lib/escher/vk/vulkan_device_queues.h"
#include "lib/escher/vk/vulkan_instance.h"

// This is the main() function for the service that implements the
// ShadertoyFactory API.
int main(int argc, const char** argv) {
  escher::GlslangInitializeProcess();
  {
    // Only enable Vulkan validation layers when in debug mode.
    escher::VulkanInstance::Params instance_params(
        {{},
         {VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
          VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
          VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME},
         false});
#if !defined(NDEBUG)
    instance_params.layer_names.insert("VK_LAYER_LUNARG_standard_validation");
#endif
    auto vulkan_instance =
        escher::VulkanInstance::New(std::move(instance_params));

    auto vulkan_device = escher::VulkanDeviceQueues::New(
        vulkan_instance, {{
                              VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
                              VK_KHR_EXTERNAL_MEMORY_FUCHSIA_EXTENSION_NAME,
                              VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
                              VK_KHR_EXTERNAL_SEMAPHORE_FUCHSIA_EXTENSION_NAME,
                          },
                          vk::SurfaceKHR()});

    escher::Escher escher(vulkan_device);

    async::Loop loop(&kAsyncLoopConfigMakeDefault);
    trace::TraceProvider trace_provider(loop.async());

    std::unique_ptr<fuchsia::sys::StartupContext> app_context(
        fuchsia::sys::StartupContext::CreateFromStartupInfo());

    shadertoy::App app(&loop, app_context.get(), &escher);
    loop.Run();
  }
  escher::GlslangFinalizeProcess();

  return 0;
}
