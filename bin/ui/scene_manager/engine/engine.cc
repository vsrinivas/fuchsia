// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/engine/engine.h"

#include <set>

#include <trace/event.h>

#include "garnet/bin/ui/scene_manager/engine/frame_scheduler.h"
#include "garnet/bin/ui/scene_manager/engine/frame_timings.h"
#include "garnet/bin/ui/scene_manager/engine/session.h"
#include "garnet/bin/ui/scene_manager/engine/session_handler.h"
#include "garnet/bin/ui/scene_manager/resources/compositor/compositor.h"
#include "garnet/bin/ui/scene_manager/resources/nodes/traversal.h"
#include "lib/escher/renderer/paper_renderer.h"
#include "lib/fxl/functional/make_copyable.h"

namespace scene_manager {

Engine::Engine(DisplayManager* display_manager,
               escher::Escher* escher,
               std::unique_ptr<escher::VulkanSwapchain> swapchain)
    : display_manager_(display_manager),
      escher_(escher),
      paper_renderer_(fxl::MakeRefCounted<escher::PaperRenderer>(escher)),
      image_factory_(std::make_unique<escher::SimpleImageFactory>(
          escher->resource_recycler(),
          escher->gpu_allocator())),
      rounded_rect_factory_(
          std::make_unique<escher::RoundedRectFactory>(escher)),
      release_fence_signaller_(std::make_unique<ReleaseFenceSignaller>(
          escher->command_buffer_sequencer())),
      swapchain_(std::move(swapchain)),
      session_count_(0) {
  FXL_DCHECK(display_manager_);
  FXL_DCHECK(escher_);
  FXL_DCHECK(swapchain_);

  InitializeFrameScheduler();
  paper_renderer_->set_sort_by_pipeline(false);
}

Engine::Engine(DisplayManager* display_manager,
               std::unique_ptr<ReleaseFenceSignaller> release_fence_signaller)
    : display_manager_(display_manager),
      escher_(nullptr),
      release_fence_signaller_(std::move(release_fence_signaller)) {
  FXL_DCHECK(display_manager_);

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

void Engine::ScheduleSessionUpdate(uint64_t presentation_time,
                                   fxl::RefPtr<Session> session) {
  if (session->is_valid()) {
    updatable_sessions_.insert({presentation_time, std::move(session)});
    ScheduleUpdate(presentation_time);
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

void Engine::CreateSession(
    ::fidl::InterfaceRequest<scenic::Session> request,
    ::fidl::InterfaceHandle<scenic::SessionListener> listener) {
  SessionId session_id = next_session_id_++;

  auto handler =
      CreateSessionHandler(session_id, std::move(request), std::move(listener));
  sessions_.insert({session_id, std::move(handler)});
  ++session_count_;
}

std::unique_ptr<DisplaySwapchain> Engine::CreateDisplaySwapchain(
    Display* display) {
  FXL_DCHECK(!display->is_claimed());
  return std::make_unique<DisplaySwapchain>(display, event_timestamper(),
                                            escher(), GetVulkanSwapchain());
}

std::unique_ptr<SessionHandler> Engine::CreateSessionHandler(
    SessionId session_id,
    ::fidl::InterfaceRequest<scenic::Session> request,
    ::fidl::InterfaceHandle<scenic::SessionListener> listener) {
  return std::make_unique<SessionHandler>(this, session_id, std::move(request),
                                          std::move(listener));
}

SessionHandler* Engine::FindSession(SessionId id) {
  auto it = sessions_.find(id);
  if (it != sessions_.end()) {
    return it->second.get();
  }
  return nullptr;
}

void Engine::TearDownSession(SessionId id) {
  auto it = sessions_.find(id);
  FXL_DCHECK(it != sessions_.end());
  if (it != sessions_.end()) {
    std::unique_ptr<SessionHandler> handler = std::move(it->second);
    sessions_.erase(it);
    FXL_DCHECK(session_count_ > 0);
    --session_count_;
    handler->TearDown();

    // Don't destroy handler immediately, since it may be the one calling
    // TearDownSession().
    fsl::MessageLoop::GetCurrent()->task_runner()->PostTask(
        fxl::MakeCopyable([handler = std::move(handler)] {}));
  }
}

void Engine::RenderFrame(const FrameTimingsPtr& timings,
                         uint64_t presentation_time,
                         uint64_t presentation_interval) {
  TRACE_DURATION("gfx", "RenderFrame", "frame_number", timings->frame_number(),
                 "time", presentation_time, "interval", presentation_interval);

  if (!ApplyScheduledSessionUpdates(presentation_time, presentation_interval))
    return;

  UpdateAndDeliverMetrics(presentation_time);

  for (auto& compositor : compositors_) {
    compositor->DrawFrame(timings, paper_renderer_.get());
  }
}

bool Engine::ApplyScheduledSessionUpdates(uint64_t presentation_time,
                                          uint64_t presentation_interval) {
  TRACE_DURATION("gfx", "ApplyScheduledSessionUpdates", "time",
                 presentation_time, "interval", presentation_interval);

  bool needs_render = false;
  while (!updatable_sessions_.empty()) {
    auto top = updatable_sessions_.begin();
    if (top->first > presentation_time)
      break;
    auto session = std::move(top->second);
    updatable_sessions_.erase(top);
    if (session) {
      needs_render |= session->ApplyScheduledUpdates(presentation_time,
                                                     presentation_interval);
    } else {
      // Corresponds to a call to ScheduleUpdate(), which always triggers a
      // render.
      needs_render = true;
    }
  }
  return needs_render;
}

escher::VulkanSwapchain Engine::GetVulkanSwapchain() const {
  FXL_DCHECK(swapchain_);
  return *(swapchain_.get());
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

}  // namespace scene_manager
