// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene/session/session_context.h"

#include "apps/mozart/src/scene/frame_scheduler.h"
#include "apps/mozart/src/scene/renderer/renderer.h"
#include "apps/mozart/src/scene/session/session.h"

namespace mozart {
namespace scene {

SessionContext::SessionContext() = default;

SessionContext::SessionContext(
    escher::Escher* escher,
    FrameScheduler* frame_scheduler,
    std::unique_ptr<escher::VulkanSwapchain> swapchain)
    : escher_(escher),
      image_factory_(escher ? std::make_unique<escher::SimpleImageFactory>(
                                  escher->resource_recycler(),
                                  escher->gpu_allocator())
                            : nullptr),
      rounded_rect_factory_(
          escher ? std::make_unique<escher::RoundedRectFactory>(escher)
                 : nullptr),
      frame_scheduler_(frame_scheduler),
      swapchain_(std::move(swapchain)) {}

SessionContext::~SessionContext() = default;

ResourceLinker& SessionContext::GetResourceLinker() {
  return resource_linker_;
}

bool SessionContext::ExportResource(ResourcePtr resource,
                                    mx::eventpair endpoint) {
  return resource_linker_.ExportResource(std::move(resource),
                                         std::move(endpoint));
}

void SessionContext::ImportResource(ImportPtr import,
                                    mozart2::ImportSpec spec,
                                    const mx::eventpair& endpoint) {
  // The import is not captured in the OnImportResolvedCallback because we don't
  // want the reference in the bind to prevent the import from being collected.
  // However, when the import dies, its handle is collected which will cause the
  // resource to expire within the resource linker. In that case, we will never
  // receive the callback with |ResolutionResult::kSuccess|.
  ResourceLinker::OnImportResolvedCallback import_resolved_callback =
      std::bind(&SessionContext::OnImportResolvedForResource,  // method
                this,                                          // target
                import.get(),  // the import that will be resolved by the linker
                std::placeholders::_1,  // the acutal object to link to import
                std::placeholders::_2   // result of the linking
                );
  resource_linker_.ImportResource(spec, endpoint, import_resolved_callback);
}

void SessionContext::OnImportResolvedForResource(
    Import* import,
    ResourcePtr actual,
    ResourceLinker::ResolutionResult resolution_result) {
  if (resolution_result == ResourceLinker::ResolutionResult::kSuccess) {
    actual->AddImport(import);
  }
}

void SessionContext::ScheduleSessionUpdate(uint64_t presentation_time,
                                           ftl::RefPtr<Session> session) {
  updatable_sessions_.push(
      std::make_pair(presentation_time, std::move(session)));

  if (frame_scheduler_) {
    frame_scheduler_->RequestFrame(presentation_time);
  } else {
    // Apply update immediately.  This is done for tests.
    FTL_LOG(WARNING)
        << "No FrameScheduler available; applying update immediately";
    OnPrepareFrame(presentation_time, 0);
  }
}

bool SessionContext::OnPrepareFrame(uint64_t presentation_time,
                                    uint64_t presentation_interval) {
  bool needs_render = false;
  while (!updatable_sessions_.empty() &&
         updatable_sessions_.top().first <= presentation_time) {
    auto session = std::move(updatable_sessions_.top().second);
    updatable_sessions_.pop();
    needs_render |= session->ApplyScheduledUpdates(presentation_time,
                                                   presentation_interval);
  }
  return needs_render;
}

escher::VulkanSwapchain SessionContext::GetVulkanSwapchain() const {
  FTL_DCHECK(swapchain_);
  return *(swapchain_.get());
}

}  // namespace scene
}  // namespace mozart
