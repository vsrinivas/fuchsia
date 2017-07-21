// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene_manager/engine/engine.h"

#include "escher/renderer/paper_renderer.h"
#include "lib/ftl/functional/make_copyable.h"

#include "apps/mozart/src/scene_manager/engine/frame_scheduler.h"
#include "apps/mozart/src/scene_manager/engine/session.h"
#include "apps/mozart/src/scene_manager/engine/session_handler.h"
#include "apps/mozart/src/scene_manager/renderer/renderer.h"

namespace scene_manager {

Engine::Engine(escher::Escher* escher,
               std::unique_ptr<FrameScheduler> frame_scheduler,
               std::unique_ptr<escher::VulkanSwapchain> swapchain)
    : escher_(escher),
      image_factory_(std::make_unique<escher::SimpleImageFactory>(
          escher->resource_recycler(),
          escher->gpu_allocator())),
      rounded_rect_factory_(
          std::make_unique<escher::RoundedRectFactory>(escher)),
      release_fence_signaller_(std::make_unique<ReleaseFenceSignaller>(
          escher->command_buffer_sequencer())),
      frame_scheduler_(std::move(frame_scheduler)),
      swapchain_(std::move(swapchain)),
      session_count_(0) {
  // Either both Escher and a FrameScheduler must be available, or neither.
  FTL_DCHECK(!escher_ == !frame_scheduler_);

  if (frame_scheduler_)
    frame_scheduler_->set_delegate(this);
}

Engine::Engine(std::unique_ptr<ReleaseFenceSignaller> r)
    : release_fence_signaller_(std::move(r)) {}

Engine::~Engine() = default;

ResourceLinker& Engine::GetResourceLinker() {
  return resource_linker_;
}

bool Engine::ExportResource(ResourcePtr resource, mx::eventpair endpoint) {
  return resource_linker_.ExportResource(std::move(resource),
                                         std::move(endpoint));
}

void Engine::ImportResource(ImportPtr import,
                            mozart2::ImportSpec spec,
                            const mx::eventpair& endpoint) {
  // The import is not captured in the OnImportResolvedCallback because we don't
  // want the reference in the bind to prevent the import from being collected.
  // However, when the import dies, its handle is collected which will cause the
  // resource to expire within the resource linker. In that case, we will never
  // receive the callback with |ResolutionResult::kSuccess|.
  ResourceLinker::OnImportResolvedCallback import_resolved_callback =
      std::bind(&Engine::OnImportResolvedForResource,  // method
                this,                                  // target
                import.get(),  // the import that will be resolved by the linker
                std::placeholders::_1,  // the acutal object to link to import
                std::placeholders::_2   // result of the linking
                );
  resource_linker_.ImportResource(spec, endpoint, import_resolved_callback);
}

void Engine::OnImportResolvedForResource(
    Import* import,
    ResourcePtr actual,
    ResourceLinker::ResolutionResult resolution_result) {
  if (resolution_result == ResourceLinker::ResolutionResult::kSuccess) {
    actual->AddImport(import);
  }
}

void Engine::ScheduleSessionUpdate(uint64_t presentation_time,
                                   ftl::RefPtr<Session> session) {
  if (session->is_valid()) {
    updatable_sessions_.push({presentation_time, std::move(session)});
    ScheduleUpdate(presentation_time);
  }
}

void Engine::ScheduleUpdate(uint64_t presentation_time) {
  if (frame_scheduler_) {
    frame_scheduler_->RequestFrame(presentation_time);
  } else {
    // Apply update immediately.  This is done for tests.
    FTL_LOG(WARNING)
        << "No FrameScheduler available; applying update immediately";
    RenderFrame(presentation_time, 0);
  }
}

void Engine::CreateSession(
    ::fidl::InterfaceRequest<mozart2::Session> request,
    ::fidl::InterfaceHandle<mozart2::SessionListener> listener) {
  SessionId session_id = next_session_id_++;

  auto handler =
      CreateSessionHandler(session_id, std::move(request), std::move(listener));
  sessions_.insert({session_id, std::move(handler)});
  ++session_count_;
}

std::unique_ptr<SessionHandler> Engine::CreateSessionHandler(
    SessionId session_id,
    ::fidl::InterfaceRequest<mozart2::Session> request,
    ::fidl::InterfaceHandle<mozart2::SessionListener> listener) {
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
  FTL_DCHECK(it != sessions_.end());
  if (it != sessions_.end()) {
    std::unique_ptr<SessionHandler> handler = std::move(it->second);
    sessions_.erase(it);
    --session_count_;
    handler->TearDown();

    // Don't destroy handler immediately, since it may be the one calling
    // TearDownSession().
    mtl::MessageLoop::GetCurrent()->task_runner()->PostTask(
        ftl::MakeCopyable([handler = std::move(handler)]{}));
  }
}

void Engine::RenderFrame(uint64_t presentation_time,
                         uint64_t presentation_interval) {
  if (!ApplyScheduledSessionUpdates(presentation_time, presentation_interval))
    return;

  for (auto renderer : renderers_) {
    renderer->DrawFrame();
  }
}

bool Engine::ApplyScheduledSessionUpdates(uint64_t presentation_time,
                                          uint64_t presentation_interval) {
  bool needs_render = false;
  while (!updatable_sessions_.empty() &&
         updatable_sessions_.top().first <= presentation_time) {
    auto session = std::move(updatable_sessions_.top().second);
    updatable_sessions_.pop();
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
  FTL_DCHECK(swapchain_);
  return *(swapchain_.get());
}

const escher::PaperRendererPtr& Engine::GetPaperRenderer() {
  if (!paper_renderer_ && escher_) {
    paper_renderer_ = escher_->NewPaperRenderer();
    paper_renderer_->set_sort_by_pipeline(false);
  }
  return paper_renderer_;
}

void Engine::AddRenderer(Renderer* renderer) {
  FTL_DCHECK(renderer);
  FTL_DCHECK(renderer->session()->engine() == this);

  FTL_CHECK(renderers_.empty()) << "Only one Renderer is currently supported.";

  bool success = renderers_.insert(renderer).second;
  FTL_DCHECK(success);
}

void Engine::RemoveRenderer(Renderer* renderer) {
  FTL_DCHECK(renderer);
  FTL_DCHECK(renderer->session()->engine() == this);

  size_t count = renderers_.erase(renderer);
  FTL_DCHECK(count == 1);
}

}  // namespace scene_manager
