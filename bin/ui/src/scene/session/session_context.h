// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/src/scene/frame_scheduler.h"
#include "apps/mozart/src/scene/resources/import.h"
#include "apps/mozart/src/scene/resources/nodes/scene.h"
#include "apps/mozart/src/scene/resources/resource_linker.h"
#include "escher/escher.h"
#include "escher/examples/common/demo_harness.h"
#include "escher/impl/gpu_uploader.h"
#include "escher/resources/resource_recycler.h"
#include "lib/escher/escher/renderer/simple_image_factory.h"
#include "lib/escher/escher/shape/rounded_rect_factory.h"

namespace mozart {
namespace scene {

class FrameScheduler;
class Session;

// Interface that describes the ways that a |Session| communicates with its
// environment.
class SessionContext : public FrameSchedulerListener {
 public:
  SessionContext();

  SessionContext(escher::Escher* escher,
                 FrameScheduler* frame_scheduler,
                 std::unique_ptr<escher::VulkanSwapchain> swapchain);

  ~SessionContext();

  ResourceLinker& GetResourceLinker();

  // Register a resource so that it can be imported into a different session
  // via ImportResourceOp.  Return true if successful, and false if the params
  // are invalid.
  bool ExportResource(ResourcePtr resource, mx::eventpair endpoint);

  // Return a new resource in the importing session that acts as a import for
  // a resource that was exported by another session.  Return nullptr if the
  // params are invalid.
  void ImportResource(ImportPtr import,
                      mozart2::ImportSpec spec,
                      const mx::eventpair& endpoint);

  escher::Escher* escher() const { return escher_; }
  escher::VulkanSwapchain GetVulkanSwapchain() const;

  vk::Device vk_device() {
    return escher_ ? escher_->vulkan_context().device : vk::Device();
  }

  escher::ResourceRecycler* escher_resource_recycler() {
    return escher_ ? escher_->resource_recycler() : nullptr;
  }

  escher::ImageFactory* escher_image_factory() { return image_factory_.get(); }

  escher::impl::GpuUploader* escher_gpu_uploader() {
    return escher_ ? escher_->gpu_uploader() : nullptr;
  }

  escher::RoundedRectFactory* escher_rounded_rect_factory() {
    return rounded_rect_factory_.get();
  }

  // Tell the FrameScheduler to schedule a frame, and remember the Session so
  // that we can tell it to apply updates when the FrameScheduler notifies us
  // via OnPrepareFrame().
  void ScheduleSessionUpdate(uint64_t presentation_time,
                             ftl::RefPtr<Session> session);
  FrameScheduler* frame_scheduler() const { return frame_scheduler_; }

 private:
  // Implement |FrameSchedulerListener|.  For each session, apply all updates
  // that should be applied before rendering and presenting a frame at
  // |presentation_time|.
  bool OnPrepareFrame(uint64_t presentation_time,
                      uint64_t presentation_interval) override;

  ResourceLinker resource_linker_;
  escher::Escher* const escher_ = nullptr;
  std::unique_ptr<escher::SimpleImageFactory> image_factory_;
  std::unique_ptr<escher::RoundedRectFactory> rounded_rect_factory_;
  FrameScheduler* const frame_scheduler_ = nullptr;
  std::unique_ptr<escher::VulkanSwapchain> swapchain_;

  // Lists all Session that have updates to apply, sorted by the earliest
  // requested presentation time of each update.
  std::priority_queue<std::pair<uint64_t, ftl::RefPtr<Session>>>
      updatable_sessions_;

  void OnImportResolvedForResource(
      Import* import,
      ResourcePtr actual,
      ResourceLinker::ResolutionResult resolution_result);

  FTL_DISALLOW_COPY_AND_ASSIGN(SessionContext);
};

}  // namespace scene
}  // namespace mozart
