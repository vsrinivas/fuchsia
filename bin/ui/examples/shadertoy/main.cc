// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/application_context.h"
#include "apps/mozart/examples/shadertoy/shadertoy_app.h"
#include "escher/escher.h"
#include "escher/escher_process_init.h"
#include "escher/vk/vulkan_device_queues.h"
#include "escher/vk/vulkan_instance.h"

// This is the main() function for the service that implements the
// ShadertoyFactory API.
int main(int argc, const char** argv) {
  escher::GlslangInitializeProcess();
  {
    auto vulkan_instance = escher::VulkanInstance::New(
        {{"VK_LAYER_LUNARG_standard_validation"}, {"VK_EXT_debug_report"}});
    auto vulkan_device = escher::VulkanDeviceQueues::New(vulkan_instance, {});

    escher::Escher escher(vulkan_device->GetVulkanContext());

    std::unique_ptr<app::ApplicationContext> app_context(
        app::ApplicationContext::CreateFromStartupInfo());

    mtl::MessageLoop loop;
    ShadertoyApp app(app_context.get(), &escher);
    loop.Run();
  }
  escher::GlslangFinalizeProcess();

  return 0;
}
