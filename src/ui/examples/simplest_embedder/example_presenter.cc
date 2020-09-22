// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/simplest_embedder/example_presenter.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

namespace simplest_embedder {

ExamplePresenter::ExamplePresenter(fuchsia::ui::scenic::Scenic* scenic)
    : session_(scenic), layers_(&session_) {
  // This would typically be done by the root Presenter.
  scenic->GetDisplayInfo([this](fuchsia::ui::gfx::DisplayInfo display_info) {
    Init(static_cast<float>(display_info.width_in_px),
         static_cast<float>(display_info.height_in_px));
  });
}

void ExamplePresenter::Init(float width, float height) {
  FX_CHECK(!compositor_);
  width_ = width;
  height_ = height;
  compositor_ = std::make_unique<scenic::DisplayCompositor>(&session_);
  compositor_->SetLayerStack(layers_);

  MaybeSetPresentationSize();
  ScenicSessionPresent();
}

void ExamplePresenter::PresentView(
    fuchsia::ui::views::ViewHolderToken view_holder_token,
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> /*presentation_request*/) {
  FX_CHECK(!presentation_) << "simplest_embedder: only a single Presentation is supported.";

  FX_LOGS(INFO) << "Presenting View.";

  presentation_ = std::make_unique<Presentation>(&session_, std::move(view_holder_token));
  layers_.AddLayer(presentation_->layer());

  MaybeSetPresentationSize();
  ScenicSessionPresent();
}

void ExamplePresenter::PresentOrReplaceView(
    fuchsia::ui::views::ViewHolderToken view_holder_token,
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation_request) {
  FX_CHECK(!presentation_) << "simplest_embedder: clobbering presentation is not supported";
  PresentView(std::move(view_holder_token), std::move(presentation_request));
};

void ExamplePresenter::MaybeSetPresentationSize() {
  if (compositor_ && presentation_) {
    presentation_->SetSize(width_, height_);
  }
}

void ExamplePresenter::ScenicSessionPresent() {
  session_.Present(0, [this](fuchsia::images::PresentationInfo info) { ScenicSessionPresent(); });
}

ExamplePresenter::Presentation::Presentation(scenic::Session* session,
                                             fuchsia::ui::views::ViewHolderToken view_holder_token)
    : layer_(session),
      view_holder_node_(session),
      view_holder_(session, std::move(view_holder_token),
                   "simplest_embedder Presentation of ShadertoyEmbedderView") {
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
  view_holder_node_.SetTranslation(0, 0, -10.f);

  ambient_light.SetColor(0.3f, 0.3f, 0.3f);
  directional_light.SetColor(0.7f, 0.7f, 0.7f);
  directional_light.SetDirection(1.f, 1.f, -2.f);
}

void ExamplePresenter::Presentation::SetSize(float width, float height) {
  layer_.SetSize(static_cast<int32_t>(width), static_cast<int32_t>(height));
  // TODO(fxbug.dev/24474): Don't hardcode Z bounds in multiple locations.
  view_holder_.SetViewProperties(0.f, 0.f, -1000.f, width, height, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
                                 0.f);
}

}  // namespace simplest_embedder
