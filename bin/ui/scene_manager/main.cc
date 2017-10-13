// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <trace-provider/provider.h>
#include <vulkan/vulkan.hpp>

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"

#include "garnet/bin/ui/scene_manager/displays/display_manager.h"
#include "garnet/bin/ui/scene_manager/scene_manager_app.h"
#include "garnet/bin/ui/scene_manager/util/vulkan_utils.h"
#include "lib/escher/escher_process_init.h"
#include "lib/escher/vk/vulkan_device_queues.h"
#include "lib/escher/vk/vulkan_instance.h"

using namespace scene_manager;

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  SceneManagerApp::Params params;
  if (!params.Setup(command_line))
    return 1;

  fsl::MessageLoop loop;
  trace::TraceProvider trace_provider(loop.async());

  std::unique_ptr<SceneManagerApp> scene_manager_app;
  vk::SurfaceKHR surface;

  // Don't initialize Vulkan and the SceneManagerApp until display is ready.
  DisplayManager display_manager;
  display_manager.WaitForDefaultDisplay([&scene_manager_app, &params,
                                         &display_manager]() {
    Display* display = display_manager.default_display();
    if (!display) {
      FXL_LOG(ERROR) << "No default display, SceneManager exiting";
      fsl::MessageLoop::GetCurrent()->PostQuitTask();
      return;
    }

    // Initialize Vulkan.
    escher::VulkanInstance::Params instance_params(
        {{},
         {VK_EXT_DEBUG_REPORT_EXTENSION_NAME, VK_KHR_SURFACE_EXTENSION_NAME,
          VK_KHR_MAGMA_SURFACE_EXTENSION_NAME},
         true});

    // Only enable Vulkan validation layers when in debug mode.
#if !defined(NDEBUG)
    instance_params.layer_names.insert("VK_LAYER_LUNARG_standard_validation");
#endif
    auto vulkan_instance =
        escher::VulkanInstance::New(std::move(instance_params));
    auto surface = CreateVulkanMagmaSurface(vulkan_instance->vk_instance());
    auto vulkan_device = escher::VulkanDeviceQueues::New(
        vulkan_instance,
        {{VK_KHR_EXTERNAL_SEMAPHORE_FUCHSIA_EXTENSION_NAME}, surface});
    escher::GlslangInitializeProcess();

    scene_manager_app = std::make_unique<SceneManagerApp>(
        &params, &display_manager, vulkan_instance, vulkan_device, surface);
  });

  loop.Run();

  // It's possible that |scene_manager_app| never got created (and therefore
  // escher::GlslangInitializeProcess() was never called).
  if (scene_manager_app)
    escher::GlslangFinalizeProcess();
  return 0;
}
