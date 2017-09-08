// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/shadertoy/client/view.h"

#include "garnet/examples/ui/shadertoy/client/glsl_strings.h"
#include "lib/ui/scenic/fidl_helpers.h"

namespace shadertoy_client {

View::View(app::ApplicationContext* application_context,
           mozart::ViewManagerPtr view_manager,
           fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request)
    : BaseView(std::move(view_manager),
               std::move(view_owner_request),
               "Shadertoy Example"),
      application_context_(application_context),
      loop_(mtl::MessageLoop::GetCurrent()),
      // TODO: we don't need to keep this around once we have used it to
      // create a Shadertoy.  What is the best way to achieve this?
      shadertoy_factory_(application_context_->ConnectToEnvironmentService<
                         mozart::example::ShadertoyFactory>()),
      start_time_(mx_time_get(MX_CLOCK_MONOTONIC)) {
  shadertoy_factory_.set_connection_error_handler([this] {
    FTL_LOG(INFO) << "Lost connection to ShadertoyFactory.";
    loop_->QuitNow();
  });

  // Create an ImagePipe and pass one end of it to the ShadertoyFactory in
  // order to obtain a Shadertoy.
  fidl::InterfaceHandle<scenic::ImagePipe> image_pipe_handle;
  auto image_pipe_request = image_pipe_handle.NewRequest();
  shadertoy_factory_->NewImagePipeShadertoy(shadertoy_.NewRequest(),
                                            std::move(image_pipe_handle));
  shadertoy_.set_connection_error_handler([this] {
    FTL_LOG(INFO) << "Lost connection to Shadertoy.";
    loop_->QuitNow();
  });

  // Set the GLSL source code for the Shadertoy.
  constexpr uint32_t kShapeWidth = 384;
  constexpr uint32_t kShapeHeight = 288;
  shadertoy_->SetResolution(kShapeWidth, kShapeHeight);
  shadertoy_->SetShaderCode(GetSeascapeSourceCode(), [this](bool success) {
    if (success) {
      FTL_LOG(INFO) << "GLSL code was successfully compiled.";
      shadertoy_->SetPaused(false);
    } else {
      FTL_LOG(ERROR) << "GLSL code compilation failed";
      loop_->QuitNow();
    }
  });

  // Pass the other end of the ImagePipe to the Session, and wrap the
  // resulting resource in a Material.
  uint32_t image_pipe_id = session()->AllocResourceId();
  session()->Enqueue(scenic_lib::NewCreateImagePipeOp(
      image_pipe_id, std::move(image_pipe_request)));
  scenic_lib::Material material(session());
  material.SetTexture(image_pipe_id);
  session()->ReleaseResource(image_pipe_id);

  // Create a rounded-rect shape to display the Shadertoy image on.
  scenic_lib::RoundedRectangle shape(session(), kShapeWidth, kShapeHeight, 80,
                                     80, 80, 80);

  constexpr size_t kNodeCount = 16;
  for (size_t i = 0; i < kNodeCount; ++i) {
    scenic_lib::ShapeNode node(session());
    node.SetShape(shape);
    node.SetMaterial(material);
    parent_node().AddChild(node);
    nodes_.push_back(std::move(node));
  }
}

View::~View() = default;

void View::OnSceneInvalidated(scenic::PresentationInfoPtr presentation_info) {
  if (!has_logical_size())
    return;

  // Compute the amount of time that has elapsed since the view was created.
  double seconds =
      static_cast<double>(presentation_info->presentation_time - start_time_) /
      1'000'000'000;

  const float kHalfWidth = logical_size().width * 0.5f;
  const float kHalfHeight = logical_size().height * 0.5f;

  for (size_t i = 0; i < nodes_.size(); ++i) {
    // Each node has a slightly different speed.
    float animation_progress = seconds * (32 + i) / 32.f;
    nodes_[i].SetTranslation(
        kHalfWidth + sin(animation_progress * 0.8) * kHalfWidth * 0.8,
        kHalfHeight + sin(animation_progress * 0.6) * kHalfHeight * 0.9,
        2.0 + i);
  }

  // The rounded-rectangles are constantly animating; invoke InvalidateScene()
  // to guarantee that OnSceneInvalidated() will be called again.
  InvalidateScene();
}

}  // namespace shadertoy_client
