// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene_manager/scene_manager_impl.h"

namespace scene_manager {

SceneManagerImpl::SceneManagerImpl(
    Display* display,
    escher::Escher* escher,
    std::unique_ptr<FrameScheduler> frame_scheduler,
    std::unique_ptr<escher::VulkanSwapchain> swapchain)
    : display_(display),
      engine_(std::make_unique<Engine>(escher,
                                       std::move(frame_scheduler),
                                       std::move(swapchain))) {}

SceneManagerImpl::SceneManagerImpl(Display* display,
                                   std::unique_ptr<Engine> engine)
    : display_(display), engine_(std::move(engine)) {}

SceneManagerImpl::~SceneManagerImpl() = default;

void SceneManagerImpl::CreateSession(
    ::fidl::InterfaceRequest<mozart2::Session> request,
    ::fidl::InterfaceHandle<mozart2::SessionListener> listener) {
  engine_->CreateSession(std::move(request), std::move(listener));
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
