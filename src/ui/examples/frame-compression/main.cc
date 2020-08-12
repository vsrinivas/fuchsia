// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/trace-provider/provider.h>

#include "compute_view.h"
#include "software_view.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/ui/base_view/view_provider_component.h"
#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/escher_process_init.h"
#include "src/ui/lib/escher/vk/vulkan_device_queues.h"
#include "src/ui/lib/escher/vk/vulkan_instance.h"

namespace {

constexpr uint32_t kShapeWidth = 640;
constexpr uint32_t kShapeHeight = 480;

}  // namespace

// fx shell "killall scenic.cmx; killall root_presenter.cmx"
//
// fx shell "present_view fuchsia-pkg://fuchsia.com/afbc#meta/afbc.cmx --AFBC"
int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());

  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    printf("fxl::SetLogSettingsFromCommandLine() failed\n");
    exit(-1);
  }

  static struct OptionEntry {
    std::string option;
    uint64_t modifier;
  } table[] = {
      {"AFBC", fuchsia::sysmem::FORMAT_MODIFIER_ARM_AFBC_16X16},
      {"LINEAR", fuchsia::sysmem::FORMAT_MODIFIER_LINEAR},
  };

  uint64_t modifier = fuchsia::sysmem::FORMAT_MODIFIER_LINEAR;
  uint32_t option_count = 0;
  for (const OptionEntry& option_entry : table) {
    if (command_line.HasOption(option_entry.option)) {
      if (option_count != 0) {
        printf("Too many modifier options.\n");
        exit(-1);
      }
      modifier = option_entry.modifier;
      option_count++;
    }
  }
  if (option_count == 0) {
    printf("Missing modifier flag such as --AFBC\n");
    exit(-1);
  }

  escher::GlslangInitializeProcess();

  escher::VulkanInstance::Params instance_params(
      {{},
       {VK_EXT_DEBUG_REPORT_EXTENSION_NAME, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME},
       false});
  if (command_line.HasOption("enable-validation-layers")) {
    auto validation_layer_name = escher::VulkanInstance::GetValidationLayerName();
    if (validation_layer_name) {
      instance_params.layer_names.insert(*validation_layer_name);
    }
  }
  auto vulkan_instance = escher::VulkanInstance::New(std::move(instance_params));
  auto vulkan_device = escher::VulkanDeviceQueues::New(
      vulkan_instance, {{
                            VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
                            VK_FUCHSIA_EXTERNAL_MEMORY_EXTENSION_NAME,
                            VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
                            VK_FUCHSIA_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
                            VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
                            VK_FUCHSIA_BUFFER_COLLECTION_EXTENSION_NAME,
                        },
                        {},
                        vk::SurfaceKHR()});
  escher::Escher escher(vulkan_device);

  bool paint_once = command_line.HasOption("paint-once");

  scenic::ViewFactory factory;
  if (command_line.HasOption("compute")) {
    factory = [modifier, paint_once,
               weak_escher = escher.GetWeakPtr()](scenic::ViewContext view_context) {
      return std::make_unique<frame_compression::ComputeView>(
          std::move(view_context), std::move(weak_escher), modifier, kShapeWidth, kShapeHeight,
          paint_once);
    };
  } else {
    factory = [modifier, paint_once](scenic::ViewContext view_context) {
      return std::make_unique<frame_compression::SoftwareView>(
          std::move(view_context), modifier, kShapeWidth, kShapeHeight, paint_once);
    };
  }

  scenic::ViewProviderComponent component(std::move(factory), &loop);
  loop.Run();

  escher::GlslangFinalizeProcess();
  return 0;
}
