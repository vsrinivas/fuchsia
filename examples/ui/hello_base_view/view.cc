// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/hello_base_view/view.h"

namespace hello_base_view {

ShadertoyEmbedderView::ShadertoyEmbedderView(
    component::StartupContext* startup_context,
    scenic::SessionPtrAndListenerRequest session_and_listener_request,
    zx::eventpair view_token)
    : scenic::BaseView(startup_context, std::move(session_and_listener_request),
                       std::move(view_token),
                       "hello_base_view ShadertoyEmbedderView"),
      node_(session()),
      background_(session()) {
  view().AddChild(node_);

  node_.AddChild(background_);
  scenic::Material background_material(session());
  background_material.SetColor(30, 30, 120, 255);
  background_.SetMaterial(background_material);
}

ShadertoyEmbedderView::~ShadertoyEmbedderView() = default;

void ShadertoyEmbedderView::LaunchShadertoyClient() {
  FXL_DCHECK(!view_holder_);

  embedded_view_info_ = LaunchAppAndCreateView("shadertoy_client");

  view_holder_ = std::make_unique<scenic::ViewHolder>(
      session(), std::move(embedded_view_info_.view_holder_token),
      "shadertoy_client for hello_base_view");

  node_.Attach(*(view_holder_.get()));
}

void ShadertoyEmbedderView::OnPropertiesChanged(
    fuchsia::ui::gfx::ViewProperties old_properties) {
  if (view_holder_) {
    view_holder_->SetViewProperties(view_properties());
  }

  InvalidateScene();
}

void ShadertoyEmbedderView::OnSceneInvalidated(
    fuchsia::images::PresentationInfo presentation_info) {
  const auto size = logical_size();
  const float width = size.x;
  const float height = size.y;

  scenic::RoundedRectangle background_shape(session(), width, height, 20, 20,
                                            80, 10);
  background_.SetShape(background_shape);
  background_.SetTranslation(width / 2.f, height / 2.f, 10.f);
}

}  // namespace hello_base_view
