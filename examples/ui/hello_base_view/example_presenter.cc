// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/hello_base_view/example_presenter.h"

namespace hello_base_view {

ExamplePresenter::ExamplePresenter(fuchsia::ui::scenic::Scenic* scenic)
    : session_(scenic), layers_(&session_) {}

void ExamplePresenter::Init(float width, float height) {
  FXL_CHECK(!compositor_);
  width_ = width;
  height_ = height;
  compositor_ = std::make_unique<scenic::DisplayCompositor>(&session_);
  compositor_->SetLayerStack(layers_);

  MaybeSetPresentationSize();
  ScenicSessionPresent();
}

void ExamplePresenter::PresentView(
    zx::eventpair view_holder_token,
    ::fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> ignored) {
  FXL_CHECK(!presentation_)
      << "hello_base_view: only a single Presentation is supported.";

  FXL_LOG(INFO) << "Presenting View.";

  presentation_ =
      std::make_unique<Presentation>(&session_, std::move(view_holder_token));
  layers_.AddLayer(presentation_->layer());

  MaybeSetPresentationSize();
  ScenicSessionPresent();
}

void ExamplePresenter::MaybeSetPresentationSize() {
  if (compositor_ && presentation_) {
    presentation_->SetSize(width_, height_);
  }
}

void ExamplePresenter::ScenicSessionPresent() {
  session_.Present(0, [this](fuchsia::images::PresentationInfo info) {
    ScenicSessionPresent();
  });
}

ExamplePresenter::Presentation::Presentation(scenic::Session* session,
                                             zx::eventpair view_holder_token)
    : layer_(session),
      view_holder_node_(session),
      view_holder_(session, std::move(view_holder_token),
                   "hello_base_view Presentation of ShadertoyEmbedderView") {
  scenic::Renderer renderer(session);
  scenic::Scene scene(session);
  scenic::Camera camera(scene);
  scenic::AmbientLight ambient_light(session);
  scenic::DirectionalLight directional_light(session);

  scenic::EntityNode root_node(session);

  layer_.SetRenderer(renderer);
  renderer.SetCamera(camera);

  // Set orthographic projection from viewing volume.
  camera.SetProjection(0.f);

  scene.AddLight(ambient_light);
  scene.AddLight(directional_light);
  scene.AddChild(view_holder_node_);

  view_holder_node_.Attach(view_holder_);
  view_holder_node_.SetTranslation(0, 0, 10.f);

  ambient_light.SetColor(0.3f, 0.3f, 0.3f);
  directional_light.SetColor(0.7f, 0.7f, 0.7f);
  directional_light.SetDirection(1.f, 1.f, -2.f);
}

void ExamplePresenter::Presentation::SetSize(float width, float height) {
  layer_.SetSize(static_cast<int32_t>(width), static_cast<int32_t>(height));
  view_holder_.SetViewProperties(0.f, 0.f, 0.f, width, height, 1000.f, 0.f, 0.f,
                                 0.f, 0.f, 0.f, 0.f);
}

}  // namespace hello_base_view
