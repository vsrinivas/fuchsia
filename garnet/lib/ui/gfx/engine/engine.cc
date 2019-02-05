// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/engine.h"

#include <fbl/string.h>
#include <fs/pseudo-dir.h>
#include <set>

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <trace/event.h>
#include <zx/time.h>

#include "garnet/lib/ui/gfx/engine/engine_renderer.h"
#include "garnet/lib/ui/gfx/engine/frame_scheduler.h"
#include "garnet/lib/ui/gfx/engine/frame_timings.h"
#include "garnet/lib/ui/gfx/engine/hardware_layer_assignment.h"
#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/engine/session_handler.h"
#include "garnet/lib/ui/gfx/resources/compositor/compositor.h"
#include "garnet/lib/ui/gfx/resources/dump_visitor.h"
#include "garnet/lib/ui/gfx/resources/nodes/traversal.h"
#include "garnet/lib/ui/gfx/util/vulkan_utils.h"
#include "garnet/lib/ui/scenic/session.h"
#include "lib/escher/renderer/batch_gpu_uploader.h"

namespace scenic_impl {
namespace gfx {

Engine::Engine(std::unique_ptr<FrameScheduler> frame_scheduler,
               DisplayManager* display_manager,
               escher::EscherWeakPtr weak_escher)
    : display_manager_(display_manager),
      escher_(std::move(weak_escher)),
      engine_renderer_(std::make_unique<EngineRenderer>(escher_)),
      image_factory_(std::make_unique<escher::ImageFactoryAdapter>(
          escher()->gpu_allocator(), escher()->resource_recycler())),
      rounded_rect_factory_(
          std::make_unique<escher::RoundedRectFactory>(escher_)),
      release_fence_signaller_(std::make_unique<escher::ReleaseFenceSignaller>(
          escher()->command_buffer_sequencer())),
      session_manager_(std::make_unique<SessionManager>()),
      frame_scheduler_(std::move(frame_scheduler)),
      imported_memory_type_index_(GetImportedMemoryTypeIndex(
          escher()->vk_physical_device(), escher()->vk_device())),
      has_vulkan_(escher_ && escher_->vk_device()),
      weak_factory_(this) {
  FXL_DCHECK(display_manager_);
  FXL_DCHECK(escher_);

  // TODO(SCN-1092): make |frame_scheduler_| non-nullable.  For testing, this
  // might entail plugging in a dummy Display.  Relates to SCN-452.
  if (frame_scheduler_)
    frame_scheduler_->set_delegate(this);
}

Engine::Engine(
    std::unique_ptr<FrameScheduler> frame_scheduler,
    DisplayManager* display_manager,
    std::unique_ptr<escher::ReleaseFenceSignaller> release_fence_signaller,
    std::unique_ptr<SessionManager> session_manager,
    escher::EscherWeakPtr weak_escher)
    : display_manager_(display_manager),
      escher_(std::move(weak_escher)),
      release_fence_signaller_(std::move(release_fence_signaller)),
      session_manager_(std::move(session_manager)),
      frame_scheduler_(std::move(frame_scheduler)),
      imported_memory_type_index_(
          escher_ ? GetImportedMemoryTypeIndex(escher_->vk_physical_device(),
                                               escher_->vk_device())
                  : 0),
      has_vulkan_(escher_ && escher_->vk_device()),
      weak_factory_(this) {
  FXL_DCHECK(display_manager_);

  // TODO(SCN-1092): make |frame_scheduler_| non-nullable.  For testing, this
  // might entail plugging in a dummy Display.  Relates to SCN-452.
  if (frame_scheduler_)
    frame_scheduler_->set_delegate(this);
}

Engine::~Engine() = default;

void Engine::ScheduleUpdate(uint64_t presentation_time) {
  // TODO(SCN-1092): make |frame_scheduler_| non-nullable.  This is feasible now
  // that we can use TestLoopFixture::RunLoopFor() to cause the scheduler to
  // render.
  if (frame_scheduler_) {
    frame_scheduler_->RequestFrame(presentation_time);
  } else {
    // Apply update immediately.  This is done for tests.
    FXL_LOG(WARNING)
        << "No FrameScheduler available; applying update immediately";
    RenderFrame(FrameTimingsPtr(), presentation_time, 0, false);
  }
}

CommandContext InitializeCommandContext(bool has_vulkan,
                                        escher::EscherWeakPtr escher,
                                        uint64_t frame_number_for_tracing) {
  return CommandContext(has_vulkan ? escher::BatchGpuUploader::New(
                                         escher, frame_number_for_tracing)
                                   : nullptr);
}

bool Engine::UpdateSessions(uint64_t presentation_time,
                            uint64_t presentation_interval,
                            uint64_t frame_number_for_tracing) {
  CommandContext command_context =
      InitializeCommandContext(has_vulkan(), escher_, frame_number_for_tracing);
  bool any_updates_were_applied =
      session_manager_->ApplyScheduledSessionUpdates(
          &command_context, presentation_time, presentation_interval);
  command_context.Flush();

  return any_updates_were_applied;
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

bool Engine::RenderFrame(const FrameTimingsPtr& timings,
                         uint64_t presentation_time,
                         uint64_t presentation_interval, bool force_render) {
  TRACE_DURATION("gfx", "RenderFrame", "frame_number", timings->frame_number(),
                 "time", presentation_time, "interval", presentation_interval);

  // TODO(SCN-1092): make |timings| non-nullable, and unconditionally use
  // timings->frame_number() below.  When this is done, uncomment the following
  // line:
  // FXL_DCHECK(timings);

  // TODO(SCN-1108): consider applying updates as each fence is signalled.
  if (!UpdateSessions(presentation_time, presentation_interval,
                      timings ? timings->frame_number() : 0) &&
      !force_render) {
    return false;
  }
  UpdateAndDeliverMetrics(presentation_time);

  // Some updates were applied; we interpret this to mean that the scene may
  // have changed, and therefore needs to be rendered.
  // TODO(SCN-1091): this is a very conservative approach that may result in
  // excessive rendering.

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
        DumpVisitor visitor(output);
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
      escher()->NewFrame("Scenic Compositor", timings->frame_number());

  bool success = true;
  for (size_t i = 0; i < hlas.size(); ++i) {
    const bool is_last_hla = (i == hlas.size() - 1);
    HardwareLayerAssignment& hla = hlas[i];

    success &= hla.swapchain->DrawAndPresentFrame(
        timings, hla,
        [is_last_hla, &frame, engine_renderer{engine_renderer_.get()}](
            zx_time_t target_presentation_time,
            const escher::ImagePtr& output_image,
            const HardwareLayerAssignment::Item hla_item,
            const escher::SemaphorePtr& acquire_semaphore,
            const escher::SemaphorePtr& frame_done_semaphore) {
          output_image->SetWaitSemaphore(acquire_semaphore);
          engine_renderer->RenderLayers(frame, target_presentation_time,
                                        output_image, hla_item.layers);
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
  TRACE_DURATION("gfx", "UpdateAndDeliverMetrics", "time", presentation_time);

  // Gather all of the scene which might need to be updated.
  std::set<Scene*> scenes;
  for (auto compositor : scene_graph_.compositors()) {
    compositor->CollectScenes(&scenes);
  }
  if (scenes.empty())
    return;

  // TODO(MZ-216): Traversing the whole graph just to compute this is pretty
  // inefficient.  We should optimize this.
  ::fuchsia::ui::gfx::Metrics metrics;
  metrics.scale_x = 1.f;
  metrics.scale_y = 1.f;
  metrics.scale_z = 1.f;
  std::vector<Node*> updated_nodes;
  for (auto scene : scenes) {
    UpdateMetrics(scene, metrics, &updated_nodes);
  }

  // TODO(MZ-216): Deliver events to sessions in batches.
  // We probably want delivery to happen somewhere else which can also
  // handle delivery of other kinds of events.  We should probably also
  // have some kind of backpointer from a session to its handler.
  for (auto node : updated_nodes) {
    if (node->session()) {
      auto event = ::fuchsia::ui::gfx::Event();
      event.set_metrics(::fuchsia::ui::gfx::MetricsEvent());
      event.metrics().node_id = node->id();
      event.metrics().metrics = node->reported_metrics();
      node->session()->EnqueueEvent(std::move(event));
    }
  }
}

// TODO(mikejurka): move this to appropriate util file
bool MetricsEquals(const ::fuchsia::ui::gfx::Metrics& a,
                   const ::fuchsia::ui::gfx::Metrics& b) {
  return a.scale_x == b.scale_x && a.scale_y == b.scale_y &&
         a.scale_z == b.scale_z;
}

void Engine::UpdateMetrics(Node* node,
                           const ::fuchsia::ui::gfx::Metrics& parent_metrics,
                           std::vector<Node*>* updated_nodes) {
  ::fuchsia::ui::gfx::Metrics local_metrics;
  local_metrics.scale_x = parent_metrics.scale_x * node->scale().x;
  local_metrics.scale_y = parent_metrics.scale_y * node->scale().y;
  local_metrics.scale_z = parent_metrics.scale_z * node->scale().z;

  if ((node->event_mask() & ::fuchsia::ui::gfx::kMetricsEventMask) &&
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
            // Recursively reschedule if cleanup is
            // incomplete.
            weak->escher_cleanup_scheduled_ = false;
            weak->CleanupEscher();
          }
        },
        kCleanupDelay);
  }
}

std::string Engine::DumpScenes() const {
  std::ostringstream output;
  DumpVisitor visitor(output);

  bool first = true;
  for (auto compositor : scene_graph_.compositors()) {
    if (first)
      first = false;
    else
      output << std::endl << "===" << std::endl << std::endl;

    compositor->Accept(&visitor);
  }
  return output.str();
}

}  // namespace gfx
}  // namespace scenic_impl
