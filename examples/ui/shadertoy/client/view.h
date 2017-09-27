// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/examples/ui/shadertoy/service/services/shadertoy.fidl.h"
#include "garnet/examples/ui/shadertoy/service/services/shadertoy_factory.fidl.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/macros.h"
#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/view_framework/base_view.h"

namespace shadertoy_client {

class View : public mozart::BaseView {
 public:
  View(app::ApplicationContext* application_context,
       mozart::ViewManagerPtr view_manager,
       fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request);

  ~View() override;

  // mozart::BaseView.
  virtual bool OnInputEvent(mozart::InputEventPtr event) override;

 private:
  // |BaseView|.
  void OnSceneInvalidated(
      scenic::PresentationInfoPtr presentation_info) override;

  app::ApplicationContext* const application_context_;
  fsl::MessageLoop* loop_;

  mozart::example::ShadertoyFactoryPtr shadertoy_factory_;
  mozart::example::ShadertoyPtr shadertoy_;

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
