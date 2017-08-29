// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene_manager/scene_manager_impl.h"

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
  FTL_CHECK(display) << "There must be a default display.";

  auto info = scenic::DisplayInfo::New();
  info->physical_width = display->width();
  info->physical_height = display->height();
  info->device_pixel_ratio = display->device_pixel_ratio();
  callback(std::move(info));
}

}  // namespace scene_manager
