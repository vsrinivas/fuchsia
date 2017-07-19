// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene_manager/scene_manager_impl.h"

#include "apps/mozart/src/scene_manager/display.h"

namespace scene_manager {

SceneManagerImpl::SceneManagerImpl(
    Display* display,
    escher::Escher* escher,
    std::unique_ptr<FrameScheduler> frame_scheduler,
    std::unique_ptr<escher::VulkanSwapchain> swapchain)
    : display_(display),
      frame_scheduler_(std::move(frame_scheduler)),
      session_context_(std::make_unique<SessionContext>(escher,
                                                        frame_scheduler_.get(),
                                                        std::move(swapchain))) {
  // Either both Escher and a FrameScheduler must be available, or neither.
  FTL_DCHECK(!escher == !frame_scheduler_);

  // If a FrameScheduler was created, introduce it to the SessionContext.
  if (frame_scheduler_) {
    frame_scheduler_->AddListener(session_context_.get());
  }
}

SceneManagerImpl::~SceneManagerImpl() {
  if (frame_scheduler_) {
    frame_scheduler_->RemoveListener(session_context_.get());
  }
}

SceneManagerImpl::SceneManagerImpl(
    Display* display,
    std::unique_ptr<SessionContext> session_context,
    std::unique_ptr<FrameScheduler> frame_scheduler)
    : display_(display),
      frame_scheduler_(std::move(frame_scheduler)),
      session_context_(std::move(session_context)) {}

void SceneManagerImpl::CreateSession(
    ::fidl::InterfaceRequest<mozart2::Session> request,
    ::fidl::InterfaceHandle<mozart2::SessionListener> listener) {
  session_context_->CreateSession(std::move(request), std::move(listener));
}

void SceneManagerImpl::GetDisplayInfo(const GetDisplayInfoCallback& callback) {
  // TODO(MZ-16): need to specify different device pixel ratio for NUC vs.
  // Acer Switch 12, and also not hardcode width/height.
  auto info = mozart2::DisplayInfo::New();
  info->width = display_->width();
  info->height = display_->height();
  info->device_pixel_ratio = display_->device_pixel_ratio();
  callback(std::move(info));
}

}  // namespace scene_manager
