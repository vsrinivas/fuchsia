// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/scenic_system.h"

#include <fs/pseudo-file.h>

#include "garnet/lib/ui/mozart/mozart.h"
#include "garnet/lib/ui/scenic/util/vulkan_utils.h"
#include "lib/app/cpp/application_context.h"
#include "lib/escher/escher_process_init.h"
#include "lib/fxl/functional/make_copyable.h"

namespace scene_manager {

ScenicSystem::ScenicSystem(mz::SystemContext context)
    : mz::TempSystemDelegate(std::move(context), false) {
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

ScenicSystem::~ScenicSystem() {
  if (escher_) {
    // It's possible that |escher_| never got created (and therefore
    // escher::GlslangInitializeProcess() was never called).
    escher::GlslangFinalizeProcess();
  }
}

std::unique_ptr<mz::CommandDispatcher> ScenicSystem::CreateCommandDispatcher(
    mz::CommandDispatcherContext context) {
  return engine_->CreateCommandDispatcher(std::move(context));
}

void ScenicSystem::Initialize() {
  Display* display = display_manager_.default_display();
  if (!display) {
    FXL_LOG(ERROR) << "No default display, Scenic system exiting";
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
  vulkan_instance_ = escher::VulkanInstance::New(std::move(instance_params));
  surface_ = CreateVulkanMagmaSurface(vulkan_instance_->vk_instance());
  vulkan_device_queues_ = escher::VulkanDeviceQueues::New(
      vulkan_instance_,
      {{VK_KHR_EXTERNAL_SEMAPHORE_FUCHSIA_EXTENSION_NAME}, surface_});

  // Initialize Escher.
  escher::GlslangInitializeProcess();
  escher_ = std::make_unique<escher::Escher>(vulkan_device_queues_);

  // Initialize the Scenic engine.
  engine_ = std::make_unique<Engine>(&display_manager_, escher_.get());

  // Create a pseudo-file that dumps alls the Scenic scenes.
  context()->app_context()->GetOrCreateDebugExportDir()->AddEntry(
      "dump-scenes",
      fbl::AdoptRef(new fs::BufferedPseudoFile([this](fbl::String* out) {
        *out = engine_->DumpScenes();
        return ZX_OK;
      })));

  SetToInitialized();
};

void ScenicSystem::GetDisplayInfoImmediately(
    const ui_mozart::Mozart::GetDisplayInfoCallback& callback) {
  FXL_DCHECK(initialized_);
  Display* display = engine_->display_manager()->default_display();
  FXL_CHECK(display) << "There must be a default display.";

  auto info = scenic::DisplayInfo::New();
  info->width_in_px = display->width_in_px();
  info->height_in_px = display->height_in_px();

  callback(std::move(info));
}

void ScenicSystem::GetDisplayInfo(
    const ui_mozart::Mozart::GetDisplayInfoCallback& callback) {
  if (initialized_) {
    GetDisplayInfoImmediately(callback);
  } else {
    run_after_initialized_.push_back(
        [this, callback]() { GetDisplayInfoImmediately(callback); });
  }
};

}  // namespace scene_manager
