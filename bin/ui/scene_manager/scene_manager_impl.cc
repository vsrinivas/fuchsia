// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/scene_manager_impl.h"

namespace scene_manager {

SceneManagerImpl::SceneManagerImpl(std::unique_ptr<Engine> engine)
    : engine_(std::move(engine)) {}

SceneManagerImpl::~SceneManagerImpl() = default;

void SceneManagerImpl::CreateSession(
    ::fidl::InterfaceRequest<scenic::Session> request,
    ::fidl::InterfaceHandle<scenic::SessionListener> listener) {
  engine_->CreateSession(std::move(request), std::move(listener));
}

void SceneManagerImpl::GetDisplayInfo(const GetDisplayInfoCallback& callback) {
  Display* display = engine_->display_manager()->default_display();
  FXL_CHECK(display) << "There must be a default display.";

  // TODO(MZ-16): Change the terminology used in the Scenic API and its clients
  // to match the new definition of display metrics.
  auto info = scenic::DisplayInfo::New();
  info->physical_width = display->metrics().width_in_px();
  info->physical_height = display->metrics().height_in_px();
  info->device_pixel_ratio = display->metrics().x_scale_in_px_per_gr();
  callback(std::move(info));
}

}  // namespace scene_manager
