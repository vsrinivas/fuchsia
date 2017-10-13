// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/shadertoy/client/view.h"

#if defined(countof)
// TODO(ZX-377): Workaround for compiler error due to Zircon defining countof()
// as a macro.  Redefines countof() using GLM_COUNTOF(), which currently
// provides a more sophisticated implementation anyway.
#undef countof
#include <glm/glm.hpp>
#define countof(X) GLM_COUNTOF(X)
#else
// No workaround required.
#include <glm/glm.hpp>
#endif
#include <glm/gtc/type_ptr.hpp>

#include "garnet/examples/ui/shadertoy/client/glsl_strings.h"
#include "lib/ui/scenic/fidl_helpers.h"

namespace shadertoy_client {

namespace {
constexpr uint32_t kShapeWidth = 384;
constexpr uint32_t kShapeHeight = 288;
}  // namespace

View::View(app::ApplicationContext* application_context,
           mozart::ViewManagerPtr view_manager,
           fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request)
    : BaseView(std::move(view_manager),
               std::move(view_owner_request),
               "Shadertoy Example"),
      application_context_(application_context),
      loop_(fsl::MessageLoop::GetCurrent()),
      // TODO: we don't need to keep this around once we have used it to
      // create a Shadertoy.  What is the best way to achieve this?
      shadertoy_factory_(application_context_->ConnectToEnvironmentService<
                         mozart::example::ShadertoyFactory>()),
      start_time_(zx_time_get(ZX_CLOCK_MONOTONIC)) {
  shadertoy_factory_.set_connection_error_handler([this] {
    FXL_LOG(INFO) << "Lost connection to ShadertoyFactory.";
    loop_->QuitNow();
  });

  // Create an ImagePipe and pass one end of it to the ShadertoyFactory in
  // order to obtain a Shadertoy.
  fidl::InterfaceHandle<scenic::ImagePipe> image_pipe_handle;
  auto image_pipe_request = image_pipe_handle.NewRequest();
  shadertoy_factory_->NewImagePipeShadertoy(shadertoy_.NewRequest(),
                                            std::move(image_pipe_handle));
  shadertoy_.set_connection_error_handler([this] {
    FXL_LOG(INFO) << "Lost connection to Shadertoy.";
    loop_->QuitNow();
  });

  // Set the GLSL source code for the Shadertoy.

  shadertoy_->SetResolution(kShapeWidth, kShapeHeight);
  shadertoy_->SetShaderCode(GetSeascapeSourceCode(), [this](bool success) {
    if (success) {
      FXL_LOG(INFO) << "GLSL code was successfully compiled.";
      shadertoy_->SetPaused(false);
    } else {
      FXL_LOG(ERROR) << "GLSL code compilation failed";
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

float View::UpdateTransition(zx_time_t presentation_time) {
  double transition_elapsed_seconds =
      static_cast<double>(presentation_time - transition_start_time_) /
      1'000'000'000;
  constexpr double kTransitionDuration = 0.5;

  float transition_param = transition_elapsed_seconds / kTransitionDuration;

  if (transition_param >= 1.f) {
    if (animation_state_ == kChangingToFourCorners) {
      animation_state_ = kFourCorners;
    } else if (animation_state_ == kChangingToSwirling) {
      animation_state_ = kSwirling;
    }
  }

  if (animation_state_ == kFourCorners) {
    transition_param = 1.f;
  } else if (animation_state_ == kSwirling) {
    transition_param = 0.f;
  } else if (animation_state_ == kChangingToSwirling) {
    transition_param = 1.f - transition_param;
  }
  return glm::smoothstep(0.f, 1.f, transition_param);
}

void View::OnSceneInvalidated(scenic::PresentationInfoPtr presentation_info) {
  if (!has_logical_size())
    return;

  // Compute the amount of time that has elapsed since the view was created.
  double seconds =
      static_cast<double>(presentation_info->presentation_time - start_time_) /
      1'000'000'000;

  float transition_param =
      UpdateTransition(presentation_info->presentation_time);

  const float kHalfWidth = logical_size().width * 0.5f;
  const float kHalfHeight = logical_size().height * 0.5f;

  for (size_t i = 0; i < nodes_.size(); ++i) {
    // Compute the translation for kSwirling mode.
    // Each node has a slightly different speed.
    float animation_progress = seconds * (32 + i) / 32.f;
    glm::vec3 swirl_translation(
        kHalfWidth + sin(animation_progress * 0.8) * kHalfWidth * 1.1,
        kHalfHeight + sin(animation_progress * 0.6) * kHalfHeight * 1.2,
        50.0 + i);

    // Compute the translation for kFourCorners mode.
    int quadrant = (i / 4) % 4;
    glm::vec3 quadrant_translation;
    if (quadrant == 0) {
      quadrant_translation =
          glm::vec3(kHalfWidth * 0.5, kHalfHeight * 0.5, 50 + i);
    } else if (quadrant == 1) {
      quadrant_translation =
          glm::vec3(kHalfWidth * 0.5, kHalfHeight * 1.5, 50 + i);
    } else if (quadrant == 2) {
      quadrant_translation =
          glm::vec3(kHalfWidth * 1.5, kHalfHeight * 0.5, 50 + i);
    } else if (quadrant == 3) {
      quadrant_translation =
          glm::vec3(kHalfWidth * 1.5, kHalfHeight * 1.5, 50 + i);
    }

    glm::vec3 translation =
        glm::mix(swirl_translation, quadrant_translation, transition_param);
    float scale = 0.7f + 0.3f * transition_param;

    nodes_[i].SetTranslation(translation.x, translation.y, translation.z);
    nodes_[i].SetScale(scale, scale, scale);
  }

  // The rounded-rectangles are constantly animating; invoke InvalidateScene()
  // to guarantee that OnSceneInvalidated() will be called again.
  InvalidateScene();
}

bool View::OnInputEvent(mozart::InputEventPtr event) {
  if (animation_state_ == kChangingToFourCorners ||
      animation_state_ == kChangingToSwirling) {
    // Ignore input until transition is complete.
    return false;
  }

  if (event->is_pointer()) {
    const mozart::PointerEventPtr& pointer = event->get_pointer();
    if (pointer->phase == mozart::PointerEvent::Phase::DOWN) {
      switch (animation_state_) {
        case kFourCorners:
          animation_state_ = kChangingToSwirling;
          transition_start_time_ = zx_time_get(ZX_CLOCK_MONOTONIC);
          return true;
        case kSwirling:
          animation_state_ = kChangingToFourCorners;
          transition_start_time_ = zx_time_get(ZX_CLOCK_MONOTONIC);
          return true;
        default:
          // This will never happen, because we checked above that we're not
          // in a transitional state.
          FXL_DCHECK(false) << "already in transition.";
      }
    }
  }
  return false;
}

}  // namespace shadertoy_client
