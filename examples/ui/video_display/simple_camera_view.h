// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <deque>
#include <list>

#include <fbl/vector.h>
#include <fuchsia/simplecamera/cpp/fidl.h>
#include <lib/app/cpp/startup_context.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fxl/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/view_framework/base_view.h>

namespace video_display {

class SimpleCameraView : public mozart::BaseView {
 public:
  SimpleCameraView(
      async::Loop* loop, fuchsia::sys::StartupContext* startup_context,
      ::fuchsia::ui::views_v1::ViewManagerPtr view_manager,
      fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner>
          view_owner_request,
      bool use_fake_camera);

  ~SimpleCameraView() override;

 private:
  // From mozart::BaseView. Called when the scene is "invalidated".
  // Invalidation should happen when the surfaces change, but not
  // necessarily when a texture changes.
  void OnSceneInvalidated(
      fuchsia::images::PresentationInfo presentation_info) override;

  scenic::ShapeNode node_;

  // Client Application:
  fuchsia::sys::Services simple_camera_provider_;
  fuchsia::sys::ComponentControllerPtr controller_;
  fuchsia::simplecamera::SimpleCameraPtr simple_camera_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SimpleCameraView);
};

}  // namespace video_display
