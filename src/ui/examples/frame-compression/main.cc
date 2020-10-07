// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/inspect/cpp/component.h>
#include <lib/trace-provider/provider.h>
#include <png.h>

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

constexpr std::string_view kCompute = "compute";
constexpr std::string_view kEnableValidationLayers = "enable-validation-layers";
constexpr std::string_view kPaintCount = "paint-count";
constexpr std::string_view kPng = "png";

constexpr uint32_t kShapeWidth = 640;
constexpr uint32_t kShapeHeight = 480;

// Inspect values.
constexpr char kSoftwareView[] = "software_view";
constexpr char kComputeView[] = "compute_view";

}  // namespace

// fx shell "killall scenic.cmx; killall root_presenter.cmx"
//
// fx shell "present_view fuchsia-pkg://fuchsia.com/frame-compression#meta/frame-compression.cmx
// --AFBC"
int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  sys::ComponentInspector inspector(context.get());

  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  FX_CHECK(fxl::SetLogSettingsFromCommandLine(command_line))
      << "fxl::SetLogSettingsFromCommandLine() failed";

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
      FX_CHECK(!option_count) << "Too many modifier options";
      modifier = option_entry.modifier;
      option_count++;
    }
  }
  FX_CHECK(option_count) << "Missing modifier flag such as --AFBC";

  uint32_t width = kShapeWidth;
  uint32_t height = kShapeHeight;
  uint32_t paint_count = 0xffffffff;
  FILE* png_fp = nullptr;

  if (command_line.HasOption(kPng)) {
    std::string png_path;
    FX_CHECK(command_line.GetOptionValue(kPng, &png_path)) << "Missing --" << kPng << " argument";
    png_fp = fopen(png_path.c_str(), "r");
    FX_CHECK(png_fp) << "failed to open: " << png_path;

    png_infop info_ptr;
    auto png_ptr = frame_compression::BaseView::CreatePngReadStruct(png_fp, &info_ptr);
    width = png_get_image_width(png_ptr, info_ptr);
    height = png_get_image_height(png_ptr, info_ptr);
    frame_compression::BaseView::DestroyPngReadStruct(png_ptr, info_ptr);
    paint_count = 1;
  }

  if (command_line.HasOption(kPaintCount)) {
    std::string count;
    FX_CHECK(command_line.GetOptionValue(kPaintCount, &count))
        << "Missing --" << kPaintCount << " argument";
    paint_count = strtoul(count.c_str(), nullptr, 10);
  }

  escher::GlslangInitializeProcess();

  escher::VulkanInstance::Params instance_params(
      {{},
       {VK_EXT_DEBUG_REPORT_EXTENSION_NAME, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME},
       false});
  if (command_line.HasOption(kEnableValidationLayers)) {
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

  scenic::ViewFactory factory;
  if (command_line.HasOption(kCompute)) {
    factory = [modifier, width, height, paint_count, png_fp, inspector = &inspector,
               weak_escher = escher.GetWeakPtr()](scenic::ViewContext view_context) {
      return std::make_unique<frame_compression::ComputeView>(
          std::move(view_context), std::move(weak_escher), modifier, width, height, paint_count,
          png_fp, inspector->root().CreateChild(kComputeView));
    };
  } else {
    factory = [modifier, width, height, paint_count, png_fp,
               inspector = &inspector](scenic::ViewContext view_context) {
      return std::make_unique<frame_compression::SoftwareView>(
          std::move(view_context), modifier, width, height, paint_count, png_fp,
          inspector->root().CreateChild(kSoftwareView));
    };
  }

  {
    scenic::ViewProviderComponent component(std::move(factory), &loop, context.get());
    loop.Run();
  }

  if (png_fp) {
    fclose(png_fp);
  }
  escher::GlslangFinalizeProcess();
  return 0;
}
