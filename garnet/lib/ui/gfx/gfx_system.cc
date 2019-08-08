// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/gfx_system.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/vfs/cpp/pseudo_file.h>
#include <zircon/assert.h>

#include <trace/event.h>

#include "garnet/lib/ui/gfx/engine/session_handler.h"
#include "garnet/lib/ui/gfx/resources/dump_visitor.h"
#include "garnet/lib/ui/gfx/screenshotter.h"
#include "garnet/lib/ui/gfx/util/collection_utils.h"
#include "garnet/lib/ui/scenic/scenic.h"
#include "src/ui/lib/escher/escher_process_init.h"
#include "src/ui/lib/escher/fs/hack_filesystem.h"
#include "src/ui/lib/escher/util/check_vulkan_support.h"

namespace scenic_impl {
namespace gfx {

static const uint32_t kDumpScenesBufferCapacity = 1024 * 64;
const char* GfxSystem::kName = "GfxSystem";

GfxSystem::GfxSystem(SystemContext context, Display* display, Engine* engine,
                     escher::EscherWeakPtr escher)
    : System(std::move(context)),
      escher_(std::move(escher)),
      display_(display),
      engine_(engine),
      session_manager_(this->context()->inspect_node()->CreateChild("SessionManager")),
      weak_factory_(this) {
  FXL_DCHECK(display_);
  FXL_DCHECK(engine_);

  // Create a pseudo-file that dumps alls the Scenic scenes.
  this->context()->app_context()->outgoing()->debug_dir()->AddEntry(
      "dump-scenes", std::make_unique<vfs::PseudoFile>(
                         kDumpScenesBufferCapacity,
                         [this](std::vector<uint8_t>* output, size_t max_file_size) {
                           std::ostringstream ostream;
                           std::unordered_set<GlobalId, GlobalId::Hash> visited_resources;
                           engine_->DumpScenes(ostream, &visited_resources);
                           DumpSessionMapResources(ostream, &visited_resources);
                           auto outstr = ostream.str();
                           ZX_DEBUG_ASSERT(outstr.length() <= max_file_size);
                           output->resize(outstr.length());
                           std::copy(outstr.begin(), outstr.end(), output->begin());
                           return ZX_OK;
                         },
                         nullptr));
}

CommandDispatcherUniquePtr GfxSystem::CreateCommandDispatcher(CommandDispatcherContext context) {
  return session_manager_.CreateCommandDispatcher(std::move(context), engine_->session_context());
}

escher::EscherUniquePtr GfxSystem::CreateEscher(sys::ComponentContext* app_context) {
  // TODO(SCN-1109): VulkanIsSupported() should not be used in production.
  // It tries to create a VkInstance and VkDevice, and immediately deletes them
  // regardless of success/failure.
  if (!escher::VulkanIsSupported()) {
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
           VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
       },
       kRequiresSurface});

  // Only enable Vulkan validation layers when in debug mode.
#if !defined(NDEBUG)
  instance_params.layer_names.insert("VK_LAYER_LUNARG_standard_validation");
#endif
  auto vulkan_instance = escher::VulkanInstance::New(std::move(instance_params));

  // Tell Escher not to filter out queues that don't support presentation.
  // The display manager only supports a single connection, so none of the
  // available queues will support presentation.  This is OK, because we use
  // the display manager API to present frames directly, instead of using
  // Vulkan swapchains.
  escher::VulkanDeviceQueues::Params device_queues_params(
      {{
           VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
           VK_FUCHSIA_EXTERNAL_MEMORY_EXTENSION_NAME,
           VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
           VK_FUCHSIA_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
           VK_FUCHSIA_BUFFER_COLLECTION_EXTENSION_NAME,
           VK_KHR_MAINTENANCE1_EXTENSION_NAME,
           VK_KHR_BIND_MEMORY_2_EXTENSION_NAME,
           VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
       },
       {
           VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME,
       },
       vk::SurfaceKHR(),
       escher::VulkanDeviceQueues::Params::kDisableQueueFilteringForPresent});

  auto vulkan_device_queues =
      escher::VulkanDeviceQueues::New(vulkan_instance, device_queues_params);

  // Provide a PseudoDir where the gfx system can register debugging services.
  auto debug_dir = std::make_shared<vfs::PseudoDir>();
  app_context->outgoing()->debug_dir()->AddSharedEntry("gfx", debug_dir);
  auto shader_fs = escher::HackFilesystem::New(debug_dir);
  {
    bool success = shader_fs->InitializeWithRealFiles(
        {"shaders/model_renderer/main.frag", "shaders/model_renderer/main.vert",
         "shaders/model_renderer/default_position.vert",
         "shaders/model_renderer/shadow_map_generation.frag",
         "shaders/model_renderer/shadow_map_lighting.frag",
         "shaders/model_renderer/wobble_position.vert", "shaders/paper/common/use.glsl",
         "shaders/paper/frag/main_ambient_light.frag", "shaders/paper/frag/main_point_light.frag",
         "shaders/paper/vert/compute_model_space_position.vert",
         "shaders/paper/vert/compute_world_space_position.vert",
         "shaders/paper/vert/main_shadow_volume_extrude.vert",
         "shaders/paper/vert/vertex_attributes.vert"});
    FXL_DCHECK(success) << "Failed to init shader files.";
  }

  VkDebugReportCallbackEXT debug_report_callback;
  {
    VkDebugReportCallbackCreateInfoEXT dbgCreateInfo;
    dbgCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
    dbgCreateInfo.pNext = NULL;
    dbgCreateInfo.pfnCallback = HandleDebugReport;
    dbgCreateInfo.pUserData = NULL;
    dbgCreateInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT |
                          VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;

    // We use the C API here due to dynamically loading the extension function.
    VkResult result = vulkan_instance->proc_addrs().CreateDebugReportCallbackEXT(
        vulkan_instance->vk_instance(), &dbgCreateInfo, nullptr, &debug_report_callback);
    FXL_CHECK(result == VK_SUCCESS);
  }

  // Initialize Escher.
  escher::GlslangInitializeProcess();
  return escher::EscherUniquePtr(new escher::Escher(vulkan_device_queues, std::move(shader_fs)),
                                 // Custom deleter.
                                 // The vulkan instance is a stack variable, but it is a
                                 // fxl::RefPtr, so we can store by value.
                                 [=](escher::Escher* escher) {
                                   escher::GlslangFinalizeProcess();
                                   vulkan_instance->proc_addrs().DestroyDebugReportCallbackEXT(
                                       vulkan_instance->vk_instance(), debug_report_callback,
                                       nullptr);
                                   delete escher;
                                 });
}

void GfxSystem::DumpSessionMapResources(
    std::ostream& output, std::unordered_set<GlobalId, GlobalId::Hash>* visited_resources) {
  FXL_DCHECK(visited_resources);

  // Iterate through all sessions to find Nodes that weren't reachable from any
  // compositor.  When such a Node is found, we walk up the tree to find the
  // un-reachable sub-tree root, and then dump that. All visited Resources are
  // added to |visited_resources|, so that they are not printed again later.
  output << "============================================================\n";
  output << "============================================================\n\n";
  output << "Detached Nodes (unreachable by any Compositor): \n";
  for (auto& [session_id, session_handler] : session_manager_.sessions()) {
    const std::unordered_map<ResourceId, ResourcePtr>& resources =
        session_handler->session()->resources()->map();
    for (auto& [resource_id, resource_ptr] : resources) {
      auto visited_resource_iter = visited_resources->find(GlobalId(session_id, resource_id));
      if (visited_resource_iter == visited_resources->end()) {
        FXL_DCHECK(resource_ptr);  // Should always be valid.

        if (resource_ptr->IsKindOf<Node>()) {
          // Attempt to find the root of this detached tree of Nodes.
          Node* root_node = resource_ptr->As<Node>().get();

          while (Node* new_root = root_node->parent()) {
            auto visited_node_iter = visited_resources->find(GlobalId(session_id, new_root->id()));
            if (visited_node_iter != visited_resources->end()) {
              FXL_NOTREACHED() << "Unvisited child should not have a visited parent!";
            }

            root_node = new_root;
          }

          // Dump the entire detached Node tree, starting from the root.  This
          // will also mark everything in the tree as visited.
          DumpVisitor visitor(DumpVisitor::VisitorContext(output, visited_resources));
          root_node->Accept(&visitor);

          output << "\n===\n\n";
        }
      }
    }
  }

  // Dump any detached resources which could not be reached by a compositor
  // or a Node tree.
  output << "============================================================\n";
  output << "============================================================\n\n";
  output << "Other Detached Resources (unreachable by any Compositor): \n";
  for (auto& [session_id, session_handler] : session_manager_.sessions()) {
    const std::unordered_map<ResourceId, ResourcePtr>& resources =
        session_handler->session()->resources()->map();
    for (auto& [resource_id, resource_ptr] : resources) {
      auto visited_resource_iter = visited_resources->find(GlobalId(session_id, resource_id));
      if (visited_resource_iter == visited_resources->end()) {
        FXL_DCHECK(resource_ptr);  // Should always be valid.

        DumpVisitor visitor(DumpVisitor::VisitorContext(output, visited_resources));
        resource_ptr->Accept(&visitor);

        output << "\n===\n\n";
      }
    }
  }
}

void GfxSystem::GetDisplayInfo(fuchsia::ui::scenic::Scenic::GetDisplayInfoCallback callback) {
  auto info = ::fuchsia::ui::gfx::DisplayInfo();
  info.width_in_px = display_->width_in_px();
  info.height_in_px = display_->height_in_px();

  callback(std::move(info));
};

void GfxSystem::TakeScreenshot(fuchsia::ui::scenic::Scenic::TakeScreenshotCallback callback) {
  Screenshotter::TakeScreenshot(engine_, std::move(callback));
}

void GfxSystem::GetDisplayOwnershipEvent(
    fuchsia::ui::scenic::Scenic::GetDisplayOwnershipEventCallback callback) {
  // These constants are defined as raw hex in the FIDL file, so we confirm here that they are the
  // same values as the expected constants in the ZX headers.
  static_assert(fuchsia::ui::scenic::displayNotOwnedSignal == ZX_USER_SIGNAL_0, "Bad constant");
  static_assert(fuchsia::ui::scenic::displayOwnedSignal == ZX_USER_SIGNAL_1, "Bad constant");

  zx::event dup;
  if (display_->ownership_event().duplicate(ZX_RIGHTS_BASIC, &dup) != ZX_OK) {
    FXL_LOG(ERROR) << "## Vulkan display event dup error";
  } else {
    callback(std::move(dup));
  }
}

// Applies scheduled updates to a session. If the update fails, the session is
// killed. Returns true if a new render is needed, false otherwise.
SessionUpdater::UpdateResults GfxSystem::UpdateSessions(
    std::unordered_set<SessionId> sessions_to_update, zx::time presentation_time,
    uint64_t trace_id) {
  SessionUpdater::UpdateResults update_results;

  if (!command_context_) {
    command_context_ = std::make_optional<CommandContext>(
        escher_ ? escher::BatchGpuUploader::New(escher_->GetWeakPtr(), trace_id) : nullptr);
  }

  for (auto session_id : sessions_to_update) {
    TRACE_DURATION("gfx", "GfxSystem::UpdateSessions", "session_id", session_id,
                   "target_presentation_time", presentation_time.get());
    auto session_handler = session_manager_.FindSessionHandler(session_id);
    if (!session_handler) {
      // This means the session that requested the update died after the
      // request. Requiring the scene to be re-rendered to reflect the session's
      // disappearance is probably desirable. ImagePipe also relies on this to
      // be true, since it calls ScheduleUpdate() in it's destructor.
      update_results.needs_render = true;
      continue;
    }

    auto session = session_handler->session();

    auto apply_results =
        session->ApplyScheduledUpdates(&(command_context_.value()), presentation_time);

    // If update fails, kill the entire client session.
    if (!apply_results.success) {
      // TODO(SCN-1485): schedule another frame because the session's contents
      // will be removed from the scene.  We could insert |session_id| into
      // |update_results.sessions_to_reschedule|, but it's probably cleaner to
      // handle this uniformly with the case that the client abruptly closes
      // the channel.
      session_handler->KillSession();
    } else {
      if (!apply_results.all_fences_ready) {
        update_results.sessions_to_reschedule.insert(session_id);

        // NOTE: one might be tempted to CHECK that the
        // callbacks/image_pipe_callbacks are empty at this point, reasoning
        // that if some fences aren't ready, then no callbacks should be
        // collected.  However, the session may have had multiple queued
        // updates, some of which had all fences ready and therefore contributed
        // callbacks.
      }
      //  Collect the callbacks to be passed back in the |UpdateResults|.
      MoveAllItemsFromQueueToQueue(&apply_results.callbacks, &update_results.present_callbacks);
      MoveAllItemsFromQueueToQueue(&apply_results.image_pipe_callbacks,
                                   &update_results.present_callbacks);
    }

    if (apply_results.needs_render) {
      TRACE_FLOW_BEGIN("gfx", "needs_render", needs_render_count_);
      update_results.needs_render = true;
      ++needs_render_count_;
    }
  }

  return update_results;
}

void GfxSystem::PrepareFrame(zx::time presentation_time, uint64_t trace_id) {
  while (processed_needs_render_count_ < needs_render_count_) {
    TRACE_FLOW_END("gfx", "needs_render", processed_needs_render_count_);
    ++processed_needs_render_count_;
  }

  if (command_context_) {
    command_context_->Flush();
    command_context_.reset();
  }
}

VkBool32 GfxSystem::HandleDebugReport(VkDebugReportFlagsEXT flags_in,
                                      VkDebugReportObjectTypeEXT object_type_in, uint64_t object,
                                      size_t location, int32_t message_code,
                                      const char* pLayerPrefix, const char* pMessage,
                                      void* pUserData) {
  vk::DebugReportFlagsEXT flags(static_cast<vk::DebugReportFlagBitsEXT>(flags_in));
  vk::DebugReportObjectTypeEXT object_type(
      static_cast<vk::DebugReportObjectTypeEXT>(object_type_in));

  // TODO(SCN-704) remove this block
  if (object_type == vk::DebugReportObjectTypeEXT::eDeviceMemory && message_code == 385878038) {
    FXL_LOG(WARNING) << "Ignoring Vulkan Memory Type Error, see SCN-704";
  }

#define VK_DEBUG_REPORT_MESSAGE                                                                \
  pMessage << " (layer: " << pLayerPrefix << "  code: " << message_code                        \
           << "  object-type: " << vk::to_string(object_type) << "  object: " << object << ")" \
           << std::endl;

  bool fatal = false;
  if (flags == vk::DebugReportFlagBitsEXT::eInformation) {
    FXL_LOG(INFO) << "## Vulkan Information: " << VK_DEBUG_REPORT_MESSAGE;
  } else if (flags == vk::DebugReportFlagBitsEXT::eWarning) {
    FXL_LOG(WARNING) << "## Vulkan Warning: " << VK_DEBUG_REPORT_MESSAGE;
  } else if (flags == vk::DebugReportFlagBitsEXT::ePerformanceWarning) {
    FXL_LOG(WARNING) << "## Vulkan Performance Warning: " << VK_DEBUG_REPORT_MESSAGE;
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
    FXL_LOG(ERROR) << "## Vulkan Unknown Message Type (flags: " << vk::to_string(flags) << "): ";
  }

  // Crash immediately on fatal errors.
  FX_CHECK(!fatal);

  return false;

#undef VK_DEBUG_REPORT_MESSAGE
}

}  // namespace gfx
}  // namespace scenic_impl
