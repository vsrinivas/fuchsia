// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trace-provider/provider.h>

#include "lib/app/cpp/application_context.h"
#include "garnet/examples/ui/shadertoy/service/app.h"
#include "escher/escher.h"
#include "escher/escher_process_init.h"
#include "escher/vk/vulkan_device_queues.h"
#include "escher/vk/vulkan_instance.h"

// This is the main() function for the service that implements the
// ShadertoyFactory API.
int main(int argc, const char** argv) {
  escher::GlslangInitializeProcess();
  {
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

    fsl::MessageLoop loop;
    trace::TraceProvider trace_provider(loop.async());

    std::unique_ptr<app::ApplicationContext> app_context(
        app::ApplicationContext::CreateFromStartupInfo());

    shadertoy::App app(app_context.get(), &escher);
    loop.Run();
  }
  escher::GlslangFinalizeProcess();

  return 0;
}
