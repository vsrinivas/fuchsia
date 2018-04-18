// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/gfx_system.h"

#include <fs/pseudo-file.h>

#include "garnet/lib/ui/gfx/screenshotter.h"
#include "garnet/lib/ui/gfx/util/vulkan_utils.h"
#include "garnet/lib/ui/scenic/scenic.h"
#include "garnet/public/lib/escher/util/check_vulkan_support.h"
#include "lib/app/cpp/application_context.h"
#include "lib/escher/escher_process_init.h"
#include "lib/fxl/functional/make_copyable.h"

namespace scenic {
namespace gfx {

GfxSystem::GfxSystem(SystemContext context)
    : TempSystemDelegate(std::move(context), false) {
  display_manager_.WaitForDefaultDisplay([this]() {
    // Don't initialize Vulkan and the system until display is ready.
    Initialize();
    initialized_ = true;

    for (auto& closure : run_after_initialized_) {
      closure();
    }
    run_after_initialized_.clear();
  });
}

GfxSystem::~GfxSystem() {
  if (escher_) {
    // It's possible that |escher_| never got created (and therefore
    // escher::GlslangInitializeProcess() was never called).
    escher::GlslangFinalizeProcess();
  }
}

std::unique_ptr<CommandDispatcher> GfxSystem::CreateCommandDispatcher(
    CommandDispatcherContext context) {
  return engine_->session_manager()->CreateCommandDispatcher(std::move(context),
                                                             engine_.get());
}

std::unique_ptr<Engine> GfxSystem::InitializeEngine() {
  return std::make_unique<Engine>(&display_manager_, escher_.get());
}

std::unique_ptr<escher::Escher> GfxSystem::InitializeEscher() {
  // Initialize Vulkan.
  escher::VulkanInstance::Params instance_params(
      {{},
       {VK_EXT_DEBUG_REPORT_EXTENSION_NAME, VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_MAGMA_SURFACE_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME},
       true});
  // Only enable Vulkan validation layers when in debug mode.
#if !defined(NDEBUG)
  instance_params.layer_names.insert("VK_LAYER_LUNARG_standard_validation");
#endif
  vulkan_instance_ = escher::VulkanInstance::New(std::move(instance_params));
  surface_ = CreateVulkanMagmaSurface(vulkan_instance_->vk_instance());
  vulkan_device_queues_ = escher::VulkanDeviceQueues::New(
      vulkan_instance_, {{VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
                          VK_KHR_EXTERNAL_MEMORY_FUCHSIA_EXTENSION_NAME,
                          VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
                          VK_KHR_EXTERNAL_SEMAPHORE_FUCHSIA_EXTENSION_NAME},
                         surface_});

  // Initialize Escher.
  escher::GlslangInitializeProcess();
  return std::make_unique<escher::Escher>(vulkan_device_queues_);
}

void GfxSystem::Initialize() {
  Display* display = display_manager_.default_display();
  if (!display) {
    FXL_LOG(ERROR) << "No default display, Graphics system exiting";
    fsl::MessageLoop::GetCurrent()->PostQuitTask();
    return;
  }

  if (!escher::VulkanIsSupported()) {
    FXL_LOG(ERROR) << "No Vulkan on device, Graphics system exiting.";
    fsl::MessageLoop::GetCurrent()->PostQuitTask();
    return;
  }

  escher_ = InitializeEscher();

  // Initialize the Scenic engine.
  engine_ = InitializeEngine();

  // Create a pseudo-file that dumps alls the Scenic scenes.
  context()->app_context()->debug_export_dir()->AddEntry(
      "dump-scenes",
      fbl::AdoptRef(new fs::BufferedPseudoFile([this](fbl::String* out) {
        *out = engine_->DumpScenes();
        return ZX_OK;
      })));

  SetToInitialized();
};

void GfxSystem::GetDisplayInfoImmediately(
    ui::Scenic::GetDisplayInfoCallback callback) {
  FXL_DCHECK(initialized_);
  Display* display = engine_->display_manager()->default_display();
  FXL_CHECK(display) << "There must be a default display.";

  auto info = ::gfx::DisplayInfo();
  info.width_in_px = display->width_in_px();
  info.height_in_px = display->height_in_px();

  callback(std::move(info));
}

void GfxSystem::GetDisplayInfo(ui::Scenic::GetDisplayInfoCallback callback) {
  if (initialized_) {
    GetDisplayInfoImmediately(callback);
  } else {
    run_after_initialized_.push_back(
        [this, callback]() { GetDisplayInfoImmediately(callback); });
  }
};

void GfxSystem::TakeScreenshot(fidl::StringPtr filename,
                               ui::Scenic::TakeScreenshotCallback callback) {
  FXL_CHECK(initialized_);
  Screenshotter screenshotter(engine_.get());
  screenshotter.TakeScreenshot(filename.get(), callback);
}

}  // namespace gfx
}  // namespace scenic
