// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/engine.h"

#include <set>

#include <trace/event.h>

#include "garnet/lib/ui/gfx/engine/frame_scheduler.h"
#include "garnet/lib/ui/gfx/engine/frame_timings.h"
#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/engine/session_handler.h"
#include "garnet/lib/ui/gfx/resources/compositor/compositor.h"
#include "garnet/lib/ui/gfx/resources/dump_visitor.h"
#include "garnet/lib/ui/gfx/resources/nodes/traversal.h"
#include "garnet/lib/ui/gfx/swapchain/display_swapchain.h"
#include "garnet/lib/ui/gfx/swapchain/vulkan_display_swapchain.h"
#include "garnet/lib/ui/scenic/session.h"
#include "lib/escher/renderer/paper_renderer.h"
#include "lib/escher/renderer/shadow_map_renderer.h"
#include "lib/fxl/functional/make_copyable.h"

namespace scene_manager {

Engine::Engine(DisplayManager* display_manager, escher::Escher* escher)
    : display_manager_(display_manager),
      escher_(escher),
      paper_renderer_(escher::PaperRenderer::New(escher)),
      shadow_renderer_(
          escher::ShadowMapRenderer::New(escher,
                                         paper_renderer_->model_data(),
                                         paper_renderer_->model_renderer())),
      image_factory_(std::make_unique<escher::SimpleImageFactory>(
          escher->resource_recycler(),
          escher->gpu_allocator())),
      rounded_rect_factory_(
          std::make_unique<escher::RoundedRectFactory>(escher)),
      release_fence_signaller_(std::make_unique<escher::ReleaseFenceSignaller>(
          escher->command_buffer_sequencer())),
      weak_factory_(this) {
  FXL_DCHECK(display_manager_);
  FXL_DCHECK(escher_);

  session_manager_ = InitializeSessionManager();

  InitializeFrameScheduler();
  paper_renderer_->set_sort_by_pipeline(false);
}

Engine::Engine(
    DisplayManager* display_manager,
    std::unique_ptr<escher::ReleaseFenceSignaller> release_fence_signaller,
    escher::Escher* escher = nullptr)
    : display_manager_(display_manager),
      escher_(escher),
      release_fence_signaller_(std::move(release_fence_signaller)),
      weak_factory_(this) {
  FXL_DCHECK(display_manager_);

  session_manager_ = InitializeSessionManager();

  InitializeFrameScheduler();
}

Engine::~Engine() = default;

void Engine::InitializeFrameScheduler() {
  if (display_manager_->default_display()) {
    frame_scheduler_ =
        std::make_unique<FrameScheduler>(display_manager_->default_display());
    frame_scheduler_->set_delegate(this);
  }
}

void Engine::ScheduleUpdate(uint64_t presentation_time) {
  if (frame_scheduler_) {
    frame_scheduler_->RequestFrame(presentation_time);
  } else {
    // Apply update immediately.  This is done for tests.
    FXL_LOG(WARNING)
        << "No FrameScheduler available; applying update immediately";
    RenderFrame(FrameTimingsPtr(), presentation_time, 0);
  }
}

std::unique_ptr<Swapchain> Engine::CreateDisplaySwapchain(Display* display) {
  FXL_DCHECK(!display->is_claimed());
#if defined(SCENE_MANAGER_VULKAN_SWAPCHAIN)
  return std::make_unique<VulkanDisplaySwapchain>(display, event_timestamper(),
                                                  escher());
#else
  return std::make_unique<DisplaySwapchain>(display, event_timestamper(),
                                            escher());
#endif
}

bool Engine::RenderFrame(const FrameTimingsPtr& timings,
                         uint64_t presentation_time,
                         uint64_t presentation_interval) {
  TRACE_DURATION("gfx", "RenderFrame", "frame_number", timings->frame_number(),
                 "time", presentation_time, "interval", presentation_interval);

  if (!session_manager_->ApplyScheduledSessionUpdates(presentation_time,
                                                      presentation_interval))
    return false;

  UpdateAndDeliverMetrics(presentation_time);

  bool frame_drawn = false;
  for (auto& compositor : compositors_) {
    frame_drawn |= compositor->DrawFrame(timings, paper_renderer_.get(),
                                         shadow_renderer_.get());
  }

  // Technically, we should be able to do this only when frame_drawn == true.
  // But the cost is negligible, so do it always.
  CleanupEscher();

  return frame_drawn;
}

void Engine::AddCompositor(Compositor* compositor) {
  FXL_DCHECK(compositor);
  FXL_DCHECK(compositor->session()->engine() == this);

  bool success = compositors_.insert(compositor).second;
  FXL_DCHECK(success);
}

void Engine::RemoveCompositor(Compositor* compositor) {
  FXL_DCHECK(compositor);
  FXL_DCHECK(compositor->session()->engine() == this);

  size_t count = compositors_.erase(compositor);
  FXL_DCHECK(count == 1);
}

Compositor* Engine::GetFirstCompositor() const {
  FXL_DCHECK(!compositors_.empty());
  return compositors_.empty() ? nullptr : *compositors_.begin();
}

void Engine::UpdateAndDeliverMetrics(uint64_t presentation_time) {
  TRACE_DURATION("gfx", "UpdateAndDeliverMetrics", "time", presentation_time);

  // Gather all of the scene which might need to be updated.
  std::set<Scene*> scenes;
  for (auto compositor : compositors_) {
    compositor->CollectScenes(&scenes);
  }
  if (scenes.empty())
    return;

  // TODO(MZ-216): Traversing the whole graph just to compute this is pretty
  // inefficient.  We should optimize this.
  scenic::Metrics metrics;
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
      auto event = scenic::Event::New();
      event->set_metrics(scenic::MetricsEvent::New());
      event->get_metrics()->node_id = node->id();
      event->get_metrics()->metrics = node->reported_metrics().Clone();

      node->session()->EnqueueEvent(std::move(event));
    }
  }
}

void Engine::UpdateMetrics(Node* node,
                           const scenic::Metrics& parent_metrics,
                           std::vector<Node*>* updated_nodes) {
  scenic::Metrics local_metrics;
  local_metrics.scale_x = parent_metrics.scale_x * node->scale().x;
  local_metrics.scale_y = parent_metrics.scale_y * node->scale().y;
  local_metrics.scale_z = parent_metrics.scale_z * node->scale().z;

  if ((node->event_mask() & scenic::kMetricsEventMask) &&
      !node->reported_metrics().Equals(local_metrics)) {
    node->set_reported_metrics(local_metrics);
    updated_nodes->push_back(node);
  }

  ForEachDirectDescendantFrontToBack(
      *node, [this, &local_metrics, updated_nodes](Node* node) {
        UpdateMetrics(node, local_metrics, updated_nodes);
      });
}

std::unique_ptr<SessionManager> Engine::InitializeSessionManager() {
  return std::make_unique<SessionManager>(this);
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
    const fxl::TimeDelta kCleanupDelay = fxl::TimeDelta::FromMilliseconds(1);
    escher_cleanup_scheduled_ = true;
    fsl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
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
  DumpVisitor visitor(output);

  bool first = true;
  for (auto compositor : compositors_) {
    if (first)
      first = false;
    else
      output << std::endl << "===" << std::endl << std::endl;

    compositor->Accept(&visitor);
  }
  return output.str();
}

}  // namespace scene_manager
