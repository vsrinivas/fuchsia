// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/gfx_system.h"

#include <fs/pseudo-file.h>

#include "garnet/lib/ui/gfx/engine/default_frame_scheduler.h"
#include "garnet/lib/ui/gfx/engine/session_handler.h"
#include "garnet/lib/ui/gfx/screenshotter.h"
#include "garnet/lib/ui/gfx/util/vulkan_utils.h"
#include "garnet/lib/ui/scenic/scenic.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/escher/escher_process_init.h"
#include "lib/escher/fs/hack_filesystem.h"
#include "lib/escher/util/check_vulkan_support.h"
#include "public/lib/syslog/cpp/logger.h"

namespace scenic_impl {
namespace gfx {

GfxSystem::GfxSystem(SystemContext context,
                     std::unique_ptr<DisplayManager> display_manager)
    : TempSystemDelegate(std::move(context), false),
      display_manager_(std::move(display_manager)) {
  // TODO(SCN-1111): what are the intended implications of there being a test
  // display?  In this case, could we make DisplayManager signal that the
  // display is ready, even though it it is a test display?
  if (display_manager_->default_display() &&
      display_manager_->default_display()->is_test_display()) {
    async::PostTask(async_get_default_dispatcher(), DelayedInitClosure());
    return;
  }

  display_manager_->WaitForDefaultDisplay(DelayedInitClosure());
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

CommandDispatcherUniquePtr GfxSystem::CreateCommandDispatcher(
    CommandDispatcherContext context) {
  return engine_->session_manager()->CreateCommandDispatcher(
      std::move(context), engine_->session_context());
}

std::unique_ptr<Engine> GfxSystem::InitializeEngine() {
  return std::make_unique<Engine>(context()->app_context(),
                                  std::make_unique<DefaultFrameScheduler>(
                                      display_manager_->default_display()),
                                  display_manager_.get(),
                                  escher_->GetWeakPtr());
}

std::unique_ptr<escher::Escher> GfxSystem::InitializeEscher() {
  // TODO(SCN-1109): VulkanIsSupported() should not be used in production.
  // It tries to create a VkInstance and VkDevice, and immediately deletes them
  // regardless of success/failure.
  if (!escher::VulkanIsSupported()) {
    return nullptr;
  }

  if (!display_manager_->is_initialized()) {
    FXL_LOG(ERROR) << "No sysmem allocator available";
    return nullptr;
  }

  // Initialize Vulkan.
  constexpr bool kRequiresSurface = false;
  escher::VulkanInstance::Params instance_params(
      {{},
       {
           VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
           VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
           VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
           VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
       },
       kRequiresSurface});

// Only enable Vulkan validation layers when in debug mode.
#if !defined(NDEBUG)
  instance_params.layer_names.insert("VK_LAYER_LUNARG_standard_validation");
#endif
  vulkan_instance_ = escher::VulkanInstance::New(std::move(instance_params));

  // Tell Escher not to filter out queues that don't support presentation.
  // The display manager only supports a single connection, so none of the
  // available queues will support presentation.  This is OK, because we use
  // the display manager API to present frames directly, instead of using
  // Vulkan swapchains.
  escher::VulkanDeviceQueues::Params device_queues_params(
      {{
           VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
           VK_KHR_EXTERNAL_MEMORY_FUCHSIA_EXTENSION_NAME,
           VK_FUCHSIA_EXTERNAL_MEMORY_EXTENSION_NAME,
           VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
           VK_KHR_EXTERNAL_SEMAPHORE_FUCHSIA_EXTENSION_NAME,
           VK_FUCHSIA_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
           VK_FUCHSIA_BUFFER_COLLECTION_EXTENSION_NAME,
       },
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
        {"shaders/model_renderer/main.frag", "shaders/model_renderer/main.vert",
         "shaders/model_renderer/default_position.vert",
         "shaders/model_renderer/shadow_map_generation.frag",
         "shaders/model_renderer/shadow_map_lighting.frag",
         "shaders/model_renderer/wobble_position.vert",
         "shaders/paper/common/use.glsl",
         "shaders/paper/frag/main_ambient_light.frag",
         "shaders/paper/frag/main_point_light.frag",
         "shaders/paper/vert/compute_model_space_position.vert",
         "shaders/paper/vert/compute_world_space_position.vert",
         "shaders/paper/vert/main_shadow_volume_extrude.vert",
         "shaders/paper/vert/vertex_attributes.vert"});
    FXL_DCHECK(success) << "Failed to init shader files.";
  }

  // Initialize Escher.
  escher::GlslangInitializeProcess();
  return std::make_unique<escher::Escher>(vulkan_device_queues_,
                                          std::move(shader_fs));
}

fit::closure GfxSystem::DelayedInitClosure() {
  // This must *not* be executed  directly in the constructor, due to the use of
  // virtual methods, such as InitializeEscher() inside Initialize().
  return [this] {
    // Don't initialize Vulkan and the system until display is ready.
    Initialize();
    initialized_ = true;

    for (auto& closure : run_after_initialized_) {
      closure();
    }
    run_after_initialized_.clear();
  };
}

void GfxSystem::Initialize() {
  Display* display = display_manager_->default_display();
  if (!display) {
    FXL_LOG(ERROR) << "No default display, Graphics system exiting";
    context()->Quit();
    return;
  }

  // This is virtual, allowing tests to avoid instantiating an Escher.
  escher_ = InitializeEscher();
  if (!escher_ || !escher_->device()) {
    if (display->is_test_display()) {
      FXL_LOG(INFO) << "No Vulkan found, but using a test-only \"display\".";
    } else {
      FXL_LOG(ERROR) << "No Vulkan on device, Graphics system exiting.";
      context()->Quit();
      return;
    }
  }

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
  Display* display = display_manager_->default_display();
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
  if (initialized_) {
    Screenshotter::TakeScreenshot(engine_.get(), std::move(callback));
  } else {
    run_after_initialized_.push_back(
        [this, callback = std::move(callback)]() mutable {
          Screenshotter::TakeScreenshot(engine_.get(), std::move(callback));
        });
  }
}

void GfxSystem::GetDisplayOwnershipEventImmediately(
    fuchsia::ui::scenic::Scenic::GetDisplayOwnershipEventCallback callback) {
  FXL_DCHECK(initialized_);
  Display* display = display_manager_->default_display();

  // TODO(SCN-1109):VulkanIsSupported() should not be called by production code.
  if (escher::VulkanIsSupported()) {
    FXL_CHECK(display) << "There must be a default display.";
  }

  static_assert(fuchsia::ui::scenic::displayNotOwnedSignal == ZX_USER_SIGNAL_0,
                "Bad constant");
  static_assert(fuchsia::ui::scenic::displayOwnedSignal == ZX_USER_SIGNAL_1,
                "Bad constant");

  zx::event dup;
  if (display->ownership_event().duplicate(ZX_RIGHTS_BASIC, &dup) != ZX_OK) {
    FXL_LOG(ERROR) << "## Vulkan display event dup error";
  } else {
    callback(std::move(dup));
  }
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
    FXL_LOG(WARNING) << "Ignoring Vulkan Memory Type Error, see SCN-704";
  }

#define VK_DEBUG_REPORT_MESSAGE                                         \
  pMessage << " (layer: " << pLayerPrefix << "  code: " << message_code \
           << "  object-type: " << vk::to_string(object_type)           \
           << "  object: " << object << ")" << std::endl;

  bool fatal = false;
  if (flags == vk::DebugReportFlagBitsEXT::eInformation) {
    FXL_LOG(INFO) << "## Vulkan Information: " << VK_DEBUG_REPORT_MESSAGE;
  } else if (flags == vk::DebugReportFlagBitsEXT::eWarning) {
    FXL_LOG(WARNING) << "## Vulkan Warning: " << VK_DEBUG_REPORT_MESSAGE;
  } else if (flags == vk::DebugReportFlagBitsEXT::ePerformanceWarning) {
    FXL_LOG(WARNING) << "## Vulkan Performance Warning: "
                     << VK_DEBUG_REPORT_MESSAGE;
  } else if (flags == vk::DebugReportFlagBitsEXT::eError) {
    // Treat all errors as fatal.
    fatal = true;
    FXL_LOG(ERROR) << "## Vulkan Error: " << VK_DEBUG_REPORT_MESSAGE;
  } else if (flags == vk::DebugReportFlagBitsEXT::eDebug) {
    FXL_LOG(INFO) << "## Vulkan Debug: " << VK_DEBUG_REPORT_MESSAGE;
  } else {
    // This should never happen, unless a new value has been added to
    // vk::DebugReportFlagBitsEXT.  In that case, add a new if-clause above.
    fatal = true;
    FXL_LOG(ERROR) << "## Vulkan Unknown Message Type (flags: "
                   << vk::to_string(flags) << "): ";
  }

  // Crash immediately on fatal errors.
  FX_CHECK(!fatal);

  return false;

#undef VK_DEBUG_REPORT_MESSAGE
}

CompositorWeakPtr GfxSystem::GetCompositor(GlobalId compositor_id) const {
  return engine_->scene_graph()->GetCompositor(compositor_id);
}

gfx::Session* GfxSystem::GetSession(SessionId session_id) const {
  SessionHandler* handler =
      engine_->session_manager()->FindSessionHandler(session_id);
  return handler ? handler->session() : nullptr;
}

void GfxSystem::AddInitClosure(fit::closure closure) {
  run_after_initialized_.push_back(std::move(closure));
}

}  // namespace gfx
}  // namespace scenic_impl
