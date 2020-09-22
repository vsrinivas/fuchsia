// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_SHADERTOY_CLIENT_VIEW_H_
#define SRC_UI_EXAMPLES_SHADERTOY_CLIENT_VIEW_H_

#include <fuchsia/examples/shadertoy/cpp/fidl.h>
#include <fuchsia/math/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/ui/scenic/cpp/resources.h>

#include "src/lib/fxl/macros.h"
#include "src/lib/ui/base_view/base_view.h"

namespace shadertoy_client {

// TODO(fxbug.dev/24703): Should be folded back into ShadertoyClientView. This used to
// be common functionality for |ShadertoyClientView| and a different View class
// that used the old Views API.
class ViewImpl {
 public:
  ViewImpl(sys::ComponentContext* component_context, scenic::Session* session,
           scenic::EntityNode* parent_node);

  void OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info,
                          const fuchsia::math::SizeF& logical_size);

  bool PointerDown();

  scenic::Session* session() { return session_; }
  scenic::EntityNode* parent_node() { return parent_node_; }

  bool IsAnimating() const { return animation_state_ != kFourCorners; }

 private:
  sys::ComponentContext* const component_context_;
  scenic::Session* const session_;
  scenic::EntityNode* const parent_node_;

  fuchsia::examples::shadertoy::ShadertoyFactoryPtr shadertoy_factory_;
  fuchsia::examples::shadertoy::ShadertoyPtr shadertoy_;

  std::vector<scenic::ShapeNode> nodes_;

  enum AnimationState { kFourCorners, kSwirling, kChangingToFourCorners, kChangingToSwirling };
  AnimationState animation_state_ = kFourCorners;

  // Output a parameter that represents the progress through the current
  // transition state.  If transition is finished, set |animation_state_| to
  // new value.
  float UpdateTransition(zx_time_t presentation_time);

  // Immediately quit the current message loop.
  void QuitLoop();

  const zx_time_t start_time_;
  zx_time_t transition_start_time_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(ViewImpl);
};

// Connects to shadertoy_service to obtain an |ImagePipe| that is used as the
// material for a number of rounded-rectangles (they all share the same
// material).  When any of the rectangles is tapped, toggles between a swirling
// animation and a static layout.
class ShadertoyClientView : public scenic::BaseView {
 public:
  ShadertoyClientView(scenic::ViewContext context, const std::string& debug_name);
  ~ShadertoyClientView() = default;

 private:
  // |scenic::BaseView|.
  void OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) override;

  // |scenic::BaseView|.
  void OnPropertiesChanged(fuchsia::ui::gfx::ViewProperties old_properties) override;

  // |scenic::BaseView|.
  void OnMetricsChanged(fuchsia::ui::gfx::Metrics old_metrics) override;

  // |scenic::BaseView|.
  void OnInputEvent(fuchsia::ui::input::InputEvent event) override;

  // |scenic::SessionListener|
  virtual void OnScenicError(std::string error) override;

  ViewImpl impl_;

  bool focused_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ShadertoyClientView);
};

}  // namespace shadertoy_client

#endif  // SRC_UI_EXAMPLES_SHADERTOY_CLIENT_VIEW_H_
