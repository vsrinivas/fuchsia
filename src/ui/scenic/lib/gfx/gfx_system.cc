// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/gfx_system.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/vfs/cpp/pseudo_file.h>
#include <zircon/assert.h>

#include <set>

#include "src/ui/lib/escher/escher_process_init.h"
#include "src/ui/lib/escher/fs/hack_filesystem.h"
#include "src/ui/lib/escher/hmd/pose_buffer_latching_shader.h"
#include "src/ui/lib/escher/paper/paper_renderer_static_config.h"
#include "src/ui/lib/escher/util/check_vulkan_support.h"
#include "src/ui/scenic/lib/gfx/resources/dump_visitor.h"
#include "src/ui/scenic/lib/gfx/screenshotter.h"

namespace scenic_impl {
namespace gfx {

static const uint32_t kDumpScenesBufferCapacity = 1024 * 64;
const char* GfxSystem::kName = "GfxSystem";

GfxSystem::GfxSystem(SystemContext context, Engine* engine, Sysmem* sysmem,
                     display::DisplayManager* display_manager)
    : System(std::move(context)),
      display_manager_(display_manager),
      sysmem_(sysmem),
      engine_(engine),
      session_manager_(this->context()->inspect_node()->CreateChild("SessionManager")) {
  FX_DCHECK(engine_);

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

CommandDispatcherUniquePtr GfxSystem::CreateCommandDispatcher(
    scheduling::SessionId session_id, std::shared_ptr<EventReporter> event_reporter,
    std::shared_ptr<ErrorReporter> error_reporter) {
  return session_manager_.CreateCommandDispatcher(
      session_id, engine_->session_context(), std::move(event_reporter), std::move(error_reporter));
}

escher::EscherUniquePtr GfxSystem::CreateEscher(sys::ComponentContext* app_context) {
  // TODO(fxbug.dev/24317): VulkanIsSupported() should not be used in production.
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
  instance_params.layer_names.insert("VK_LAYER_KHRONOS_validation");
#endif
  auto vulkan_instance = escher::VulkanInstance::New(std::move(instance_params));
  auto callback_handle = vulkan_instance->RegisterDebugReportCallback(HandleDebugReport);

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
       escher::VulkanDeviceQueues::Params::kDisableQueueFilteringForPresent |
           escher::VulkanDeviceQueues::Params::kAllowProtectedMemory});

  auto vulkan_device_queues =
      escher::VulkanDeviceQueues::New(vulkan_instance, device_queues_params);

  // Provide a PseudoDir where the gfx system can register debugging services.
  auto debug_dir = std::make_shared<vfs::PseudoDir>();
  app_context->outgoing()->debug_dir()->AddSharedEntry("gfx", debug_dir);

  auto shader_fs = escher::HackFilesystem::New(debug_dir);
  {
#if ESCHER_USE_RUNTIME_GLSL
    auto paths = escher::kPaperRendererShaderPaths;
    paths.insert(paths.end(), escher::hmd::kPoseBufferLatchingPaths.begin(),
                 escher::hmd::kPoseBufferLatchingPaths.end());
    bool success = shader_fs->InitializeWithRealFiles(paths);
#else
    auto paths = escher::kPaperRendererShaderSpirvPaths;
    paths.insert(paths.end(), escher::hmd::kPoseBufferLatchingSpirvPaths.begin(),
                 escher::hmd::kPoseBufferLatchingSpirvPaths.end());
    bool success = shader_fs->InitializeWithRealFiles(paths);
#endif
    FX_DCHECK(success) << "Failed to init shader files.";
  }

  // Initialize Escher.
#if ESCHER_USE_RUNTIME_GLSL
  escher::GlslangInitializeProcess();
#endif
  return escher::EscherUniquePtr(new escher::Escher(vulkan_device_queues, std::move(shader_fs)),
                                 // Custom deleter.
                                 // The vulkan instance is a stack variable, but it is a
                                 // fxl::RefPtr, so we can store by value.
                                 [=](escher::Escher* escher) {
                                   vulkan_instance->DeregisterDebugReportCallback(callback_handle);
#if ESCHER_USE_RUNTIME_GLSL
                                   escher::GlslangFinalizeProcess();
#endif
                                   delete escher;
                                 });
}

void GfxSystem::DumpSessionMapResources(
    std::ostream& output, std::unordered_set<GlobalId, GlobalId::Hash>* visited_resources) {
  FX_DCHECK(visited_resources);

  // Iterate through all sessions to find Nodes that weren't reachable from any
  // compositor.  When such a Node is found, we walk up the tree to find the
  // un-reachable sub-tree root, and then dump that. All visited Resources are
  // added to |visited_resources|, so that they are not printed again later.
  output << "============================================================\n";
  output << "============================================================\n\n";
  output << "Detached Nodes (unreachable by any Compositor): \n";
  for (auto& [session_id, session] : session_manager_.sessions()) {
    const std::unordered_map<ResourceId, ResourcePtr>& resources = session->resources()->map();
    for (auto& [resource_id, resource_ptr] : resources) {
      auto visited_resource_iter = visited_resources->find(GlobalId(session_id, resource_id));
      if (visited_resource_iter == visited_resources->end()) {
        FX_DCHECK(resource_ptr);  // Should always be valid.

        if (resource_ptr->IsKindOf<Node>()) {
          // Attempt to find the root of this detached tree of Nodes.
          Node* root_node = resource_ptr->As<Node>().get();

          while (Node* new_root = root_node->parent()) {
            auto visited_node_iter = visited_resources->find(GlobalId(session_id, new_root->id()));
            if (visited_node_iter != visited_resources->end()) {
              FX_NOTREACHED() << "Unvisited child should not have a visited parent!";
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
  for (auto& [session_id, session] : session_manager_.sessions()) {
    const std::unordered_map<ResourceId, ResourcePtr>& resources = session->resources()->map();
    for (auto& [resource_id, resource_ptr] : resources) {
      auto visited_resource_iter = visited_resources->find(GlobalId(session_id, resource_id));
      if (visited_resource_iter == visited_resources->end()) {
        FX_DCHECK(resource_ptr);  // Should always be valid.

        DumpVisitor visitor(DumpVisitor::VisitorContext(output, visited_resources));
        resource_ptr->Accept(&visitor);

        output << "\n===\n\n";
      }
    }
  }
}

void GfxSystem::TakeScreenshot(fuchsia::ui::scenic::Scenic::TakeScreenshotCallback callback) {
  Screenshotter::TakeScreenshot(engine_, std::move(callback));
}

scheduling::SessionUpdater::UpdateResults GfxSystem::UpdateSessions(
    const std::unordered_map<scheduling::SessionId, scheduling::PresentId>& sessions_to_update,
    uint64_t frame_trace_id) {
  scheduling::SessionUpdater::UpdateResults update_results;
  CommandContext command_context{
      .sysmem = sysmem_,
      .display_manager = display_manager_,
      .warm_pipeline_cache_callback =
          [renderer = engine_->renderer()](vk::Format framebuffer_format) {
            renderer->WarmPipelineCache({framebuffer_format});
          },
      .scene_graph = engine_->scene_graph()};

  // Update scene graph and stage ViewTree updates of Annotation Views first.
  //
  // The ViewTree update of an annotation View may refer to an annotation
  // ViewHolder created during the same |UpdateSessions()| call, so we should
  // ensure that the ViewTree update that created ViewHolder's node is staged
  // earlier than the update that links nodes of annotation ViewHolder and
  // annotation View (which occurs in |Session::ApplyScheduledUpdates()|).

  // If annotation manager has annotation view holder creation requests, try
  // fulfilling them by adding the annotation ViewHolders to the SceneGraph.
  engine_->annotation_manager()->FulfillCreateRequests();

  // Session owned by AnnotationManager can also have ViewTree updates when
  // AnnotationViewHolders are created or deleted. We should stage these updates
  // into SceneGraph manually.
  engine_->annotation_manager()->StageViewTreeUpdates();

  // Apply scheduled updates to each session, and process the changes to the local session scene
  // graph.
  for (auto& [session_id, present_id] : sessions_to_update) {
    TRACE_DURATION("gfx", "GfxSystem::UpdateSessions", "session_id", session_id);
    if (auto session = session_manager_.FindSession(session_id)) {
      bool success = session->ApplyScheduledUpdates(&command_context, present_id);
      if (!success) {
        update_results.sessions_with_failed_updates.insert(session_id);
      }
    }
  }

  // Run through compositors, find the active Scene, stage it as the view tree root.
  {
    std::set<Scene*> scenes;
    for (auto compositor : engine_->scene_graph()->compositors()) {
      compositor->CollectScenes(&scenes);
    }

    ViewTreeUpdates updates;
    if (scenes.empty()) {
      updates.push_back(ViewTreeMakeGlobalRoot{.koid = ZX_KOID_INVALID});
    } else {
      if (scenes.size() > 1) {
        FX_LOGS(ERROR) << "Bug 36295 - multiple scenes active, but Scenic's ViewTree is limited to "
                          "one active focus chain.";
      }
      for (const auto scene : scenes) {
        updates.push_back(ViewTreeMakeGlobalRoot{.koid = scene->view_ref_koid()});
      }
    }
    engine_->scene_graph()->StageViewTreeUpdates(std::move(updates));
  }

  // NOTE: Call this operation in a quiescent state: when session updates are guaranteed finished!
  //       This ordering ensures that all updates are accounted for consistently, and focus-related
  //       events are dispatched just once.
  // NOTE: Failure to call this operation will result in an inconsistent SceneGraph state.
  engine_->scene_graph()->ProcessViewTreeUpdates();

  return update_results;
}

VkBool32 GfxSystem::HandleDebugReport(VkDebugReportFlagsEXT flags_in,
                                      VkDebugReportObjectTypeEXT object_type_in, uint64_t object,
                                      size_t location, int32_t message_code,
                                      const char* pLayerPrefix, const char* pMessage,
                                      void* pUserData) {
  vk::DebugReportFlagsEXT flags(static_cast<vk::DebugReportFlagBitsEXT>(flags_in));
  vk::DebugReportObjectTypeEXT object_type(
      static_cast<vk::DebugReportObjectTypeEXT>(object_type_in));

// Macro to facilitate matching messages.  Example usage:
//  if (VK_MATCH_REPORT(DescriptorSet, 0, "VUID-VkWriteDescriptorSet-descriptorType-01403")) {
//    FX_LOGS(INFO) << "ignoring descriptor set problem: " << pMessage << "\n\n";
//    return false;
//  }
#define VK_MATCH_REPORT(OTYPE, CODE, X)                                                 \
  ((object_type == vk::DebugReportObjectTypeEXT::e##OTYPE) && (message_code == CODE) && \
   (0 == strncmp(pMessage + 3, X, strlen(X) - 1)))

#define VK_DEBUG_REPORT_MESSAGE                                                                \
  pMessage << " (layer: " << pLayerPrefix << "  code: " << message_code                        \
           << "  object-type: " << vk::to_string(object_type) << "  object: " << object << ")" \
           << std::endl;

  bool fatal = false;
  if (flags == vk::DebugReportFlagBitsEXT::eInformation) {
    FX_LOGS(INFO) << "## Vulkan Information: " << VK_DEBUG_REPORT_MESSAGE;
  } else if (flags == vk::DebugReportFlagBitsEXT::eWarning) {
    FX_LOGS(WARNING) << "## Vulkan Warning: " << VK_DEBUG_REPORT_MESSAGE;
  } else if (flags == vk::DebugReportFlagBitsEXT::ePerformanceWarning) {
    FX_LOGS(WARNING) << "## Vulkan Performance Warning: " << VK_DEBUG_REPORT_MESSAGE;
  } else if (flags == vk::DebugReportFlagBitsEXT::eError) {
    // Treat all errors as fatal.
    fatal = true;
    FX_LOGS(ERROR) << "## Vulkan Error: " << VK_DEBUG_REPORT_MESSAGE;
  } else if (flags == vk::DebugReportFlagBitsEXT::eDebug) {
    FX_LOGS(INFO) << "## Vulkan Debug: " << VK_DEBUG_REPORT_MESSAGE;
  } else {
    // This should never happen, unless a new value has been added to
    // vk::DebugReportFlagBitsEXT.  In that case, add a new if-clause above.
    fatal = true;
    FX_LOGS(ERROR) << "## Vulkan Unknown Message Type (flags: " << vk::to_string(flags) << "): ";
  }

  // Crash immediately on fatal errors.
  FX_CHECK(!fatal);

  return false;

#undef VK_DEBUG_REPORT_MESSAGE
}

}  // namespace gfx
}  // namespace scenic_impl
