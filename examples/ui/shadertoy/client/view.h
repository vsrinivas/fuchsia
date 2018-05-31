// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_SHADERTOY_CLIENT_VIEW_H_
#define GARNET_EXAMPLES_UI_SHADERTOY_CLIENT_VIEW_H_

#include <lib/async-loop/cpp/loop.h>
#include <fuchsia/ui/shadertoy/cpp/fidl.h>

#include "lib/app/cpp/application_context.h"
#include "lib/fxl/macros.h"
#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/view_framework/base_view.h"

namespace shadertoy_client {

class View : public mozart::BaseView {
 public:
  View(async::Loop* loop, component::ApplicationContext* application_context,
       ::fuchsia::ui::views_v1::ViewManagerPtr view_manager,
       fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner> view_owner_request);

  ~View() override;

  // mozart::BaseView.
  virtual bool OnInputEvent(fuchsia::ui::input::InputEvent event) override;

 private:
  // |BaseView|.
  void OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) override;

  component::ApplicationContext* const application_context_;
  async::Loop* const loop_;

  ::fuchsia::ui::shadertoy::ShadertoyFactoryPtr shadertoy_factory_;
  ::fuchsia::ui::shadertoy::ShadertoyPtr shadertoy_;

  std::vector<scenic_lib::ShapeNode> nodes_;

  enum AnimationState {
    kFourCorners,
    kSwirling,
    kChangingToFourCorners,
    kChangingToSwirling
  };
  AnimationState animation_state_ = kFourCorners;

  // Output a parameter that represents the progress through the current
  // transition state.  If transition is finished, set |animation_state_| to
  // new value.
  float UpdateTransition(zx_time_t presentation_time);

  const zx_time_t start_time_;
  zx_time_t transition_start_time_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(View);
};

}  // namespace shadertoy_client

#endif  // GARNET_EXAMPLES_UI_SHADERTOY_CLIENT_VIEW_H_
