// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/gfx_system.h"

#include <fs/pseudo-file.h>

#include "garnet/lib/ui/gfx/screenshotter.h"
#include "garnet/lib/ui/gfx/util/vulkan_utils.h"
#include "garnet/lib/ui/scenic/scenic.h"
#include "garnet/public/lib/escher/util/check_vulkan_support.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/escher/escher_process_init.h"
#include "lib/escher/fs/hack_filesystem.h"
#include "public/lib/syslog/cpp/logger.h"

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
  if (vulkan_instance_) {
    vulkan_instance_->proc_addrs().DestroyDebugReportCallbackEXT(
        vulkan_instance_->vk_instance(), debug_report_callback_, nullptr);
  }
}

std::unique_ptr<CommandDispatcher> GfxSystem::CreateCommandDispatcher(
    CommandDispatcherContext context) {
  return engine_->session_manager()->CreateCommandDispatcher(std::move(context),
                                                             engine_.get());
}

std::unique_ptr<Engine> GfxSystem::InitializeEngine() {
  return std::make_unique<Engine>(&display_manager_, escher_->GetWeakPtr());
}

std::unique_ptr<escher::Escher> GfxSystem::InitializeEscher() {
  // Initialize Vulkan.
  escher::VulkanInstance::Params instance_params(
      {{},
       {
           VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
           VK_KHR_SURFACE_EXTENSION_NAME,
           VK_KHR_MAGMA_SURFACE_EXTENSION_NAME,
           VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
           VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
       },
       true});

// Only enable Vulkan validation layers when in debug mode.
#if !defined(NDEBUG)
  instance_params.layer_names.insert("VK_LAYER_LUNARG_standard_validation");
#endif
  vulkan_instance_ = escher::VulkanInstance::New(std::move(instance_params));
  surface_ = CreateVulkanMagmaSurface(vulkan_instance_->vk_instance());

  // Tell Escher not to filter out queues that don't support presentation.
  // The display manager only supports a single connection, so none of the
  // available queues will support presentation.  This is OK, because we use
  // the display manager API to present frames directly, instead of using
  // Vulkan swapchains.
  escher::VulkanDeviceQueues::Params device_queues_params(
      {{VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_FUCHSIA_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_FUCHSIA_EXTENSION_NAME},
       surface_,
       escher::VulkanDeviceQueues::Params::kDisableQueueFilteringForPresent});
  vulkan_device_queues_ =
      escher::VulkanDeviceQueues::New(vulkan_instance_, device_queues_params);

  {
    VkDebugReportCallbackCreateInfoEXT dbgCreateInfo;
    dbgCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
    dbgCreateInfo.pNext = NULL;
    dbgCreateInfo.pfnCallback = RedirectDebugReport;
    dbgCreateInfo.pUserData = this;
    dbgCreateInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT |
                          VK_DEBUG_REPORT_WARNING_BIT_EXT |
                          VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;

    // We use the C API here due to dynamically loading the extension function.
    VkResult result =
        vulkan_instance_->proc_addrs().CreateDebugReportCallbackEXT(
            vulkan_instance_->vk_instance(), &dbgCreateInfo, nullptr,
            &debug_report_callback_);
    FXL_CHECK(result == VK_SUCCESS);
  }

  // Provide a PseudoDir where the gfx system can register debugging services.
  fbl::RefPtr<fs::PseudoDir> debug_dir(fbl::AdoptRef(new fs::PseudoDir()));
  context()->app_context()->outgoing().debug_dir()->AddEntry("gfx", debug_dir);
  auto shader_fs = escher::HackFilesystem::New(debug_dir);
  {
    bool success = shader_fs->InitializeWithRealFiles(
        std::vector<escher::HackFilePath>{"shaders/model_renderer/main.vert"});
    FXL_DCHECK(success);
  }

  // Initialize Escher.
  escher::GlslangInitializeProcess();
  return std::make_unique<escher::Escher>(vulkan_device_queues_,
                                          std::move(shader_fs));
}

void GfxSystem::Initialize() {
  Display* display = display_manager_.default_display();
  if (!display) {
    FXL_LOG(ERROR) << "No default display, Graphics system exiting";
    context()->Quit();
    return;
  }

  if (!escher::VulkanIsSupported()) {
    FXL_LOG(ERROR) << "No Vulkan on device, Graphics system exiting.";
    context()->Quit();
    return;
  }

  escher_ = InitializeEscher();

  // Initialize the Scenic engine.
  engine_ = InitializeEngine();

  // Create a pseudo-file that dumps alls the Scenic scenes.
  context()->app_context()->outgoing().debug_dir()->AddEntry(
      "dump-scenes",
      fbl::AdoptRef(new fs::BufferedPseudoFile([this](fbl::String* out) {
        *out = engine_->DumpScenes();
        return ZX_OK;
      })));

  SetToInitialized();
};

void GfxSystem::GetDisplayInfoImmediately(
    fuchsia::ui::scenic::Scenic::GetDisplayInfoCallback callback) {
  FXL_DCHECK(initialized_);
  Display* display = engine_->display_manager()->default_display();
  FXL_CHECK(display) << "There must be a default display.";

  auto info = ::fuchsia::ui::gfx::DisplayInfo();
  info.width_in_px = display->width_in_px();
  info.height_in_px = display->height_in_px();

  callback(std::move(info));
}

void GfxSystem::GetDisplayInfo(
    fuchsia::ui::scenic::Scenic::GetDisplayInfoCallback callback) {
  if (initialized_) {
    GetDisplayInfoImmediately(std::move(callback));
  } else {
    run_after_initialized_.push_back(
        [this, callback = std::move(callback)]() mutable {
          GetDisplayInfoImmediately(std::move(callback));
        });
  }
};

void GfxSystem::TakeScreenshot(
    fuchsia::ui::scenic::Scenic::TakeScreenshotCallback callback) {
  FXL_CHECK(initialized_);
  Screenshotter screenshotter(engine_.get());
  screenshotter.TakeScreenshot(std::move(callback));
}

void GfxSystem::GetDisplayOwnershipEventImmediately(
    fuchsia::ui::scenic::Scenic::GetDisplayOwnershipEventCallback callback) {
  FXL_DCHECK(initialized_);
  Display* display = engine_->display_manager()->default_display();
  FXL_CHECK(display) << "There must be a default display.";

  static_assert(fuchsia::ui::scenic::displayNotOwnedSignal == ZX_USER_SIGNAL_0,
                "Bad constant");
  static_assert(fuchsia::ui::scenic::displayOwnedSignal == ZX_USER_SIGNAL_1,
                "Bad constant");

  zx::event dup;
  display->ownership_event().duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ, &dup);
  callback(std::move(dup));
}

void GfxSystem::GetDisplayOwnershipEvent(
    fuchsia::ui::scenic::Scenic::GetDisplayOwnershipEventCallback callback) {
  if (initialized_) {
    GetDisplayOwnershipEventImmediately(std::move(callback));
  } else {
    run_after_initialized_.push_back(
        [this, callback = std::move(callback)]() mutable {
          GetDisplayOwnershipEventImmediately(std::move(callback));
        });
  }
}

VkBool32 GfxSystem::HandleDebugReport(VkDebugReportFlagsEXT flags_in,
                                      VkDebugReportObjectTypeEXT object_type_in,
                                      uint64_t object, size_t location,
                                      int32_t message_code,
                                      const char* pLayerPrefix,
                                      const char* pMessage) {
  vk::DebugReportFlagsEXT flags(
      static_cast<vk::DebugReportFlagBitsEXT>(flags_in));
  vk::DebugReportObjectTypeEXT object_type(
      static_cast<vk::DebugReportObjectTypeEXT>(object_type_in));

  // TODO(SCN-704) remove this block
  if (object_type == vk::DebugReportObjectTypeEXT::eDeviceMemory &&
      message_code == 385878038) {
    FX_LOGS(WARNING) << "Ignoring Vulkan Memory Type Error, see SCN-704";
  }

  bool fatal = false;

  auto severity = FX_LOG_ERROR;

  if (flags == vk::DebugReportFlagBitsEXT::eInformation) {
    FX_LOGS(INFO) << "## Vulkan Information: ";
    severity = FX_LOG_INFO;
  } else if (flags == vk::DebugReportFlagBitsEXT::eWarning) {
    FX_LOGS(WARNING) << "## Vulkan Warning: ";
    severity = FX_LOG_WARNING;
  } else if (flags == vk::DebugReportFlagBitsEXT::ePerformanceWarning) {
    FX_LOGS(WARNING) << "## Vulkan Performance Warning: ";
    severity = FX_LOG_WARNING;
  } else if (flags == vk::DebugReportFlagBitsEXT::eError) {
    // Treat all errors as fatal.
    fatal = true;
    FX_LOGS(ERROR) << "## Vulkan Error: ";
    severity = FX_LOG_ERROR;
  } else if (flags == vk::DebugReportFlagBitsEXT::eDebug) {
    FX_LOGS(INFO) << "## Vulkan Debug: ";
    severity = FX_LOG_INFO;
  } else {
    // This should never happen, unless a new value has been added to
    // vk::DebugReportFlagBitsEXT.  In that case, add a new if-clause above.
    fatal = true;
    FX_LOGS(ERROR) << "## Vulkan Unknown Message Type (flags: "
                   << vk::to_string(flags) << "): ";
  }

  FX_LOGST_WITH_SEVERITY(severity, nullptr)
      << pMessage << " (layer: " << pLayerPrefix << "  code: " << message_code
      << "  object-type: " << vk::to_string(object_type)
      << "  object: " << object << ")" << std::endl;

  // Crash immediately on fatal errors.
  FX_CHECK(!fatal);

  return false;
}

}  // namespace gfx
}  // namespace scenic
