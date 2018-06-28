// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_SHADERTOY_CLIENT_VIEW_H_
#define GARNET_EXAMPLES_UI_SHADERTOY_CLIENT_VIEW_H_

#include <fuchsia/examples/shadertoy/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>

#include "lib/app/cpp/startup_context.h"
#include "lib/fxl/macros.h"
#include "lib/ui/scenic/cpp/base_view.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/scenic/cpp/view_factory.h"
#include "lib/ui/view_framework/base_view.h"

namespace shadertoy_client {

// Common functionality for |OldView| and |NewView| classes, below.
// TODO(SCN-589): Should be folded back into the latter when the former dies.
class ViewImpl {
 public:
  ViewImpl(fuchsia::sys::StartupContext* startup_context,
           scenic::Session* session, scenic::EntityNode* parent_node);

  void OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info,
                          const fuchsia::math::SizeF& logical_size);

  bool PointerDown();

  scenic::Session* session() { return session_; }
  scenic::EntityNode* parent_node() { return parent_node_; }

  bool IsAnimating() const { return animation_state_ != kFourCorners; }

 private:
  fuchsia::sys::StartupContext* const startup_context_;
  scenic::Session* const session_;
  scenic::EntityNode* const parent_node_;

  fuchsia::examples::shadertoy::ShadertoyFactoryPtr shadertoy_factory_;
  fuchsia::examples::shadertoy::ShadertoyPtr shadertoy_;

  std::vector<scenic::ShapeNode> nodes_;

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

  // Immediately quit the current message loop.
  void QuitLoop();

  const zx_time_t start_time_;
  zx_time_t transition_start_time_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(ViewImpl);
};

// Views v1, deprecated.
class OldView : public mozart::BaseView {
 public:
  OldView(fuchsia::sys::StartupContext* startup_context,
          ::fuchsia::ui::views_v1::ViewManagerPtr view_manager,
          fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner>
              view_owner_request);

  ~OldView() override;

  // |mozart::BaseView|.
  virtual bool OnInputEvent(fuchsia::ui::input::InputEvent event) override;

 private:
  scenic::EntityNode root_node_;
  ViewImpl impl_;

  // |mozart::BaseView|.
  void OnSceneInvalidated(
      fuchsia::images::PresentationInfo presentation_info) override;

  FXL_DISALLOW_COPY_AND_ASSIGN(OldView);
};

// Connects to shadertoy_service to obtain an |ImagePipe| that is used as the
// material for a number of rounded-rectangles (they all share the same
// material).  When any of the rectangles is tapped, toggles between a swirling
// animation and a static layout.
class NewView : public scenic::BaseView {
 public:
  NewView(scenic::ViewFactoryArgs args, const std::string& debug_name);

  ~NewView() override;

 private:
  // |scenic::BaseView|.
  void OnSceneInvalidated(
      fuchsia::images::PresentationInfo presentation_info) override;

  // |scenic::BaseView|.
  void OnPropertiesChanged(
      fuchsia::ui::gfx::ViewProperties old_properties) override;

  // |scenic::SessionListener|
  virtual void OnError(::fidl::StringPtr error) override;

  scenic::EntityNode root_node_;
  ViewImpl impl_;

  FXL_DISALLOW_COPY_AND_ASSIGN(NewView);
};

}  // namespace shadertoy_client

#endif  // GARNET_EXAMPLES_UI_SHADERTOY_CLIENT_VIEW_H_
