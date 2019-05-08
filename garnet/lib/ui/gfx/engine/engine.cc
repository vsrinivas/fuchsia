// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/engine.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/zx/time.h>
#include <trace/event.h>

#include <set>
#include <string>
#include <unordered_set>

#include "garnet/lib/ui/gfx/engine/frame_scheduler.h"
#include "garnet/lib/ui/gfx/engine/frame_timings.h"
#include "garnet/lib/ui/gfx/engine/hardware_layer_assignment.h"
#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/engine/session_handler.h"
#include "garnet/lib/ui/gfx/id.h"
#include "garnet/lib/ui/gfx/resources/compositor/compositor.h"
#include "garnet/lib/ui/gfx/resources/dump_visitor.h"
#include "garnet/lib/ui/gfx/resources/nodes/traversal.h"
#include "garnet/lib/ui/scenic/session.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/util/fuchsia_utils.h"

namespace scenic_impl {
namespace gfx {

CommandContext::CommandContext(
    std::unique_ptr<escher::BatchGpuUploader> uploader)
    : batch_gpu_uploader_(std::move(uploader)) {}

void CommandContext::Flush() {
  if (batch_gpu_uploader_) {
    // Submit regardless of whether or not there are updates to release the
    // underlying CommandBuffer so the pool and sequencer don't stall out.
    // TODO(ES-115) to remove this restriction.
    batch_gpu_uploader_->Submit();
  }
}

Engine::Engine(sys::ComponentContext* component_context,
               std::unique_ptr<FrameScheduler> frame_scheduler,
               std::unique_ptr<SessionManager> session_manager,
               DisplayManager* display_manager,
               escher::EscherWeakPtr weak_escher, inspect::Node inspect_node)
    : display_manager_(display_manager),
      escher_(std::move(weak_escher)),
      engine_renderer_(std::make_unique<EngineRenderer>(escher_)),
      event_timestamper_(component_context),
      image_factory_(std::make_unique<escher::ImageFactoryAdapter>(
          escher()->gpu_allocator(), escher()->resource_recycler())),
      rounded_rect_factory_(
          std::make_unique<escher::RoundedRectFactory>(escher_)),
      release_fence_signaller_(std::make_unique<escher::ReleaseFenceSignaller>(
          escher()->command_buffer_sequencer())),
      session_manager_(std::move(session_manager)),
      frame_scheduler_(std::move(frame_scheduler)),
      has_vulkan_(escher_ && escher_->vk_device()),
      inspect_node_(std::move(inspect_node)),
      weak_factory_(this) {
  FXL_DCHECK(frame_scheduler_);
  FXL_DCHECK(session_manager_);
  FXL_DCHECK(display_manager_);
  FXL_DCHECK(escher_);

  InitializeFrameScheduler();
  InitializeInspectObjects();
}

Engine::Engine(
    sys::ComponentContext* component_context,
    std::unique_ptr<FrameScheduler> frame_scheduler,
    DisplayManager* display_manager,
    std::unique_ptr<escher::ReleaseFenceSignaller> release_fence_signaller,
    std::unique_ptr<SessionManager> session_manager,
    escher::EscherWeakPtr weak_escher)
    : display_manager_(display_manager),
      escher_(std::move(weak_escher)),
      event_timestamper_(component_context),
      release_fence_signaller_(std::move(release_fence_signaller)),
      session_manager_(std::move(session_manager)),
      frame_scheduler_(std::move(frame_scheduler)),
      has_vulkan_(escher_ && escher_->vk_device()),
      weak_factory_(this) {
  FXL_DCHECK(frame_scheduler_);
  FXL_DCHECK(display_manager_);

  InitializeFrameScheduler();
  InitializeInspectObjects();
}

void Engine::InitializeFrameScheduler() {
  auto weak = weak_factory_.GetWeakPtr();
  frame_scheduler_->SetDelegate(FrameSchedulerDelegate{
      /* FrameRenderer */ weak, /* SessionUpdater */ weak});
}

void Engine::InitializeInspectObjects() {
  inspect_scene_dump_ =
      inspect_node_.CreateLazyStringProperty("scene_dump", [this] {
        if (scene_graph_.compositors().empty()) {
          return std::string("(no compositors)");
        }
        std::ostringstream output;
        output << std::endl;
        for (auto& c : scene_graph_.compositors()) {
          output << "========== BEGIN COMPOSITOR DUMP ======================"
                 << std::endl;
          DumpVisitor visitor(DumpVisitor::VisitorContext(output, nullptr));
          c->Accept(&visitor);
          output << "============ END COMPOSITOR DUMP ======================";
        }
        return output.str();
      });
}

// Helper for RenderFrame().  Generate a mapping between a Compositor's Layer
// resources and the hardware layers they should be displayed on.
// TODO(SCN-1088): there should be a separate mechanism that is responsible
// for inspecting the compositor's resource tree and optimizing the assignment
// of rendered content to hardware display layers.
std::optional<HardwareLayerAssignment> GetHardwareLayerAssignment(
    const Compositor& compositor) {
  // TODO(SCN-1098): this is a placeholder; currently only a single hardware
  // layer is supported, and we don't know its ID (it is hidden within the
  // DisplayManager implementation), so we just say 0.
  std::vector<Layer*> layers = compositor.GetDrawableLayers();
  if (layers.empty() || !compositor.swapchain()) {
    return {};
  }
  return {
      {.items = {{
           .hardware_layer_id = 0,
           .layers = std::move(layers),
       }},
       .swapchain = compositor.swapchain()},
  };
}

CommandContext Engine::CreateCommandContext(uint64_t trace_id) {
  return CommandContext(has_vulkan()
                            ? escher::BatchGpuUploader::New(escher_, trace_id)
                            : nullptr);
}

// Applies scheduled updates to a session. If the update fails, the session is
// killed. Returns true if a new render is needed, false otherwise.
SessionUpdater::UpdateResults Engine::UpdateSessions(
    std::unordered_set<SessionId> sessions_to_update,
    zx_time_t presentation_time, uint64_t trace_id) {
  SessionUpdater::UpdateResults update_results;
  for (auto session_id : sessions_to_update) {
    auto session_handler = session_manager_->FindSessionHandler(session_id);
    if (!session_handler) {
      // This means the session that requested the update died after the
      // request. Requiring the scene to be re-rendered to reflect the session's
      // disappearance is probably desirable. ImagePipe also relies on this to
      // be true, since it calls ScheduleUpdate() in it's destructor.
      update_results.needs_render = true;
      continue;
    }

    auto session = session_handler->session();

    if (!command_context_) {
      command_context_ =
          std::make_optional<CommandContext>(CreateCommandContext(trace_id));
    }
    auto apply_results = session->ApplyScheduledUpdates(
        &(command_context_.value()), presentation_time, needs_render_count_);

    // If update fails, kill the entire client session.
    if (!apply_results.success) {
      session_handler->KillSession();
    } else {
      if (!apply_results.all_fences_ready) {
        update_results.sessions_to_reschedule.insert(session_id);
      }

      //  Collect the callbacks for later.
      while (!apply_results.callbacks.empty()) {
        callbacks_this_frame_.push(std::move(apply_results.callbacks.front()));
        apply_results.callbacks.pop();
      }
      while (!apply_results.image_pipe_callbacks.empty()) {
        callbacks_this_frame_.push(
            std::move(apply_results.image_pipe_callbacks.front()));
        apply_results.image_pipe_callbacks.pop();
      }
    }

    if (apply_results.needs_render) {
      update_results.needs_render = true;
      ++needs_render_count_;
    }
  }

  return update_results;
}

void Engine::RatchetPresentCallbacks() {
  while (!callbacks_this_frame_.empty()) {
    pending_callbacks_.push(std::move(callbacks_this_frame_.front()));
    callbacks_this_frame_.pop();
  }
}

void Engine::SignalSuccessfulPresentCallbacks(
    fuchsia::images::PresentationInfo presentation_info) {
  while (!pending_callbacks_.empty()) {
    // TODO(SCN-1346): Make this unique per session via id().
    TRACE_FLOW_BEGIN("gfx", "present_callback",
                     presentation_info.presentation_time);
    pending_callbacks_.front()(presentation_info);
    pending_callbacks_.pop();
  }
}

bool Engine::RenderFrame(const FrameTimingsPtr& timings,
                         zx_time_t presentation_time) {
  uint64_t frame_number = timings->frame_number();

  // NOTE: this name is important for benchmarking.  Do not remove or modify it
  // without also updating the "process_gfx_trace.go" script.
  TRACE_DURATION("gfx", "RenderFrame", "frame_number", frame_number, "time",
                 presentation_time);

  while (processed_needs_render_count_ < needs_render_count_) {
    TRACE_FLOW_END("gfx", "needs_render", processed_needs_render_count_);
    ++processed_needs_render_count_;
  }

  // Flush work to the gpu.
  command_context_->Flush();
  command_context_.reset();

  UpdateAndDeliverMetrics(presentation_time);

  // TODO(SCN-1089): the FrameTimings are passed to the Compositor's swapchain
  // to notify when the frame is finished rendering, presented, dropped, etc.
  // This doesn't make any sense if there are multiple compositors.
  FXL_DCHECK(scene_graph_.compositors().size() <= 1);

  std::vector<HardwareLayerAssignment> hlas;
  for (auto& compositor : scene_graph_.compositors()) {
    if (auto hla = GetHardwareLayerAssignment(*compositor)) {
      hlas.push_back(std::move(hla.value()));

      // Verbose logging of the entire Compositor resource tree.
      if (FXL_VLOG_IS_ON(3)) {
        std::ostringstream output;
        DumpVisitor visitor(DumpVisitor::VisitorContext(output, nullptr));
        compositor->Accept(&visitor);
        FXL_VLOG(3) << "Compositor dump\n" << output.str();
      }
    } else {
      // Nothing to be drawn; either the Compositor has no layers to draw or
      // it has no valid Swapchain.  The latter will be true if Escher/Vulkan
      // is unavailable for whatever reason.
    }
  }
  if (hlas.empty()) {
    // No compositor has any renderable content.
    return false;
  }

  escher::FramePtr frame =
      escher()->NewFrame("Scenic Compositor", frame_number);

  bool success = true;
  for (size_t i = 0; i < hlas.size(); ++i) {
    const bool is_last_hla = (i == hlas.size() - 1);
    HardwareLayerAssignment& hla = hlas[i];

    success &= hla.swapchain->DrawAndPresentFrame(
        timings, hla,
        [is_last_hla, &frame, escher{escher_},
         engine_renderer{engine_renderer_.get()}](
            zx_time_t target_presentation_time,
            const escher::ImagePtr& output_image,
            const HardwareLayerAssignment::Item hla_item,
            const escher::SemaphorePtr& acquire_semaphore,
            const escher::SemaphorePtr& frame_done_semaphore) {
          output_image->SetWaitSemaphore(acquire_semaphore);
          engine_renderer->RenderLayers(frame, target_presentation_time,
                                        output_image, hla_item.layers);

          // Create a flow event that ends in the magma system driver.
          zx::event semaphore_event =
              GetEventForSemaphore(escher->device(), frame_done_semaphore);
          zx_info_handle_basic_t info;
          zx_status_t status = semaphore_event.get_info(
              ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
          ZX_DEBUG_ASSERT(status == ZX_OK);
          TRACE_FLOW_BEGIN("gfx", "semaphore", info.koid);

          if (!is_last_hla) {
            frame->SubmitPartialFrame(frame_done_semaphore);
          } else {
            frame->EndFrame(frame_done_semaphore, nullptr);
          }
        });
  }
  if (!success) {
    // TODO(SCN-1089): what is the proper behavior when some swapchains
    // are displayed and others aren't?  This isn't currently an issue because
    // there is only one Compositor; see above.
    FXL_DCHECK(hlas.size() == 1);
    return false;
  }

  CleanupEscher();
  return true;
}

void Engine::UpdateAndDeliverMetrics(uint64_t presentation_time) {
  // NOTE: this name is important for benchmarking.  Do not remove or modify it
  // without also updating the "process_gfx_trace.go" script.
  TRACE_DURATION("gfx", "UpdateAndDeliverMetrics", "time", presentation_time);

  // Gather all of the scene which might need to be updated.
  std::set<Scene*> scenes;
  for (auto compositor : scene_graph_.compositors()) {
    compositor->CollectScenes(&scenes);
  }
  if (scenes.empty())
    return;

  // TODO(SCN-216): Traversing the whole graph just to compute this is pretty
  // inefficient.  We should optimize this.
  fuchsia::ui::gfx::Metrics metrics;
  metrics.scale_x = 1.f;
  metrics.scale_y = 1.f;
  metrics.scale_z = 1.f;
  std::vector<Node*> updated_nodes;
  for (auto scene : scenes) {
    UpdateMetrics(scene, metrics, &updated_nodes);
  }

  // TODO(SCN-216): Deliver events to sessions in batches.
  // We probably want delivery to happen somewhere else which can also
  // handle delivery of other kinds of events.  We should probably also
  // have some kind of backpointer from a session to its handler.
  for (auto node : updated_nodes) {
    if (node->session()) {
      fuchsia::ui::gfx::Event event;
      event.set_metrics(::fuchsia::ui::gfx::MetricsEvent());
      event.metrics().node_id = node->id();
      event.metrics().metrics = node->reported_metrics();
      node->session()->EnqueueEvent(std::move(event));
    }
  }
}

// TODO(mikejurka): move this to appropriate util file
bool MetricsEquals(const fuchsia::ui::gfx::Metrics& a,
                   const fuchsia::ui::gfx::Metrics& b) {
  return a.scale_x == b.scale_x && a.scale_y == b.scale_y &&
         a.scale_z == b.scale_z;
}

void Engine::UpdateMetrics(Node* node,
                           const fuchsia::ui::gfx::Metrics& parent_metrics,
                           std::vector<Node*>* updated_nodes) {
  fuchsia::ui::gfx::Metrics local_metrics;
  local_metrics.scale_x = parent_metrics.scale_x * node->scale().x;
  local_metrics.scale_y = parent_metrics.scale_y * node->scale().y;
  local_metrics.scale_z = parent_metrics.scale_z * node->scale().z;

  if ((node->event_mask() & fuchsia::ui::gfx::kMetricsEventMask) &&
      !MetricsEquals(node->reported_metrics(), local_metrics)) {
    node->set_reported_metrics(local_metrics);
    updated_nodes->push_back(node);
  }

  ForEachDirectDescendantFrontToBack(
      *node, [this, &local_metrics, updated_nodes](Node* node) {
        UpdateMetrics(node, local_metrics, updated_nodes);
      });
}

void Engine::CleanupEscher() {
  // Either there is already a cleanup scheduled (meaning that this was already
  // called recently), or there is no Escher because we're running tests.
  if (!escher_ || escher_cleanup_scheduled_) {
    return;
  }
  // Only trace when there is the possibility of doing work.
  TRACE_DURATION("gfx", "Engine::CleanupEscher");

  if (!escher_->Cleanup()) {
    // Wait long enough to give GPU work a chance to finish.
    const zx::duration kCleanupDelay = zx::msec(1);
    escher_cleanup_scheduled_ = true;
    async::PostDelayedTask(
        async_get_default_dispatcher(),
        [weak = weak_factory_.GetWeakPtr()] {
          if (weak) {
            // Recursively reschedule if cleanup is incomplete.
            weak->escher_cleanup_scheduled_ = false;
            weak->CleanupEscher();
          }
        },
        kCleanupDelay);
  }
}

std::string Engine::DumpScenes() const {
  std::ostringstream output;
  std::unordered_set<GlobalId, GlobalId::Hash> visited_resources;

  // Dump all Compositors and all transitively-reachable Resources.
  // Remember the set of visited resources; the next step will be to dump the
  // unreachable resources.
  output << "Compositors: \n";
  for (auto compositor : scene_graph_.compositors()) {
    DumpVisitor visitor(
        DumpVisitor::VisitorContext(output, &visited_resources));

    compositor->Accept(&visitor);
    output << "\n===\n\n";
  }

  // Iterate through all sessions to find Nodes that weren't reachable from any
  // compositor.  When such a Node is found, we walk up the tree to find the
  // un-reachable sub-tree root, and then dump that. All visited Resources are
  // added to |visited_resources|, so that they are not printed again later.
  output << "============================================================\n";
  output << "============================================================\n\n";
  output << "Detached Nodes (unreachable by any Compositor): \n";
  for (auto& [session_id, session_handler] : session_manager_->sessions()) {
    const std::unordered_map<ResourceId, ResourcePtr>& resources =
        session_handler->session()->resources()->map();
    for (auto& [resource_id, resource_ptr] : resources) {
      auto visited_resource_iter =
          visited_resources.find(GlobalId(session_id, resource_id));
      if (visited_resource_iter == visited_resources.end()) {
        FXL_DCHECK(resource_ptr);  // Should always be valid.

        if (resource_ptr->IsKindOf<Node>()) {
          // Attempt to find the root of this detached tree of Nodes.
          Node* root_node = resource_ptr->As<Node>().get();

          while (Node* new_root = root_node->parent()) {
            auto visited_node_iter =
                visited_resources.find(GlobalId(session_id, new_root->id()));
            if (visited_node_iter != visited_resources.end()) {
              FXL_NOTREACHED()
                  << "Unvisited child should not have a visited parent!";
            }

            root_node = new_root;
          }

          // Dump the entire detached Node tree, starting from the root.  This
          // will also mark everything in the tree as visited.
          DumpVisitor visitor(
              DumpVisitor::VisitorContext(output, &visited_resources));
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
  for (auto& [session_id, session_handler] : session_manager_->sessions()) {
    const std::unordered_map<ResourceId, ResourcePtr>& resources =
        session_handler->session()->resources()->map();
    for (auto& [resource_id, resource_ptr] : resources) {
      auto visited_resource_iter =
          visited_resources.find(GlobalId(session_id, resource_id));
      if (visited_resource_iter == visited_resources.end()) {
        FXL_DCHECK(resource_ptr);  // Should always be valid.

        DumpVisitor visitor(
            DumpVisitor::VisitorContext(output, &visited_resources));
        resource_ptr->Accept(&visitor);

        output << "\n===\n\n";
      }
    }
  }

  return output.str();
}

}  // namespace gfx
}  // namespace scenic_impl
