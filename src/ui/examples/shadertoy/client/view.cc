// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/shadertoy/client/view.h"

// clang-format off
#include "src/ui/lib/glm_workaround/glm_workaround.h"
// clang-format on

#include <glm/gtc/type_ptr.hpp>
#include <lib/async/default.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <lib/syslog/cpp/macros.h>

#include "src/ui/examples/shadertoy/client/glsl_strings.h"

namespace shadertoy_client {

namespace {
constexpr uint32_t kShapeWidth = 384;
constexpr uint32_t kShapeHeight = 288;
}  // namespace

ViewImpl::ViewImpl(sys::ComponentContext* component_context, scenic::Session* sess,
                   scenic::EntityNode* parent_node_in)
    : component_context_(component_context),
      session_(sess),
      parent_node_(parent_node_in),
      // TODO: we don't need to keep this around once we have used it to create a Shadertoy.  What
      // is the best way to achieve this?
      shadertoy_factory_(
          component_context_->svc()->Connect<fuchsia::examples::shadertoy::ShadertoyFactory>()),
      start_time_(zx_clock_get_monotonic()) {
  shadertoy_factory_.set_error_handler([this](zx_status_t status) {
    FX_LOGS(INFO) << "Lost connection to ShadertoyFactory.";
    QuitLoop();
  });

  // Create an ImagePipe and pass one end of it to the ShadertoyFactory in order to obtain a
  // Shadertoy.
  fidl::InterfaceHandle<fuchsia::images::ImagePipe2> image_pipe_handle;
  auto image_pipe_request = image_pipe_handle.NewRequest();
  shadertoy_factory_->NewImagePipeShadertoy(shadertoy_.NewRequest(), std::move(image_pipe_handle));
  shadertoy_.set_error_handler([this](zx_status_t status) {
    FX_LOGS(INFO) << "Lost connection to Shadertoy.";
    QuitLoop();
  });

  // Set the GLSL source code for the Shadertoy.

  shadertoy_->SetResolution(kShapeWidth, kShapeHeight);
  shadertoy_->SetShaderCode(GetSeascapeSourceCode(), [this](bool success) {
    if (success) {
      FX_LOGS(INFO) << "GLSL code was successfully compiled.";
      shadertoy_->SetPaused(false);
    } else {
      FX_LOGS(ERROR) << "GLSL code compilation failed";
      QuitLoop();
    }
  });

  // Pass the other end of the ImagePipe to the Session, and wrap the resulting resource in a
  // Material.
  uint32_t image_pipe_id = session()->AllocResourceId();
  session()->Enqueue(scenic::NewCreateImagePipe2Cmd(image_pipe_id, std::move(image_pipe_request)));
  scenic::Material material(session());
  material.SetTexture(image_pipe_id);
  session()->ReleaseResource(image_pipe_id);

  // Create a rounded-rect shape to display the Shadertoy image on.
  scenic::RoundedRectangle shape(session(), kShapeWidth, kShapeHeight, 80, 80, 80, 80);

  constexpr size_t kNodeCount = 16;
  for (size_t i = 0; i < kNodeCount; ++i) {
    scenic::ShapeNode node(session());
    node.SetShape(shape);
    node.SetMaterial(material);
    parent_node()->AddChild(node);
    nodes_.push_back(std::move(node));
  }
}

void ViewImpl::OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info,
                                  const fuchsia::math::SizeF& logical_size) {
  // Compute the amount of time that has elapsed since the view was created.
  double seconds =
      static_cast<double>(presentation_info.presentation_time - start_time_) / 1'000'000'000;

  float transition_param = UpdateTransition(presentation_info.presentation_time);

  const float kHalfWidth = logical_size.width * 0.5f;
  const float kHalfHeight = logical_size.height * 0.5f;

  for (size_t i = 0; i < nodes_.size(); ++i) {
    // Compute the translation for kSwirling mode.  Each node has a slightly different speed.
    const double animation_progress = seconds * static_cast<double>(32 + i) / 32;
    const long elevation = -1 * (50 + 20 * i);
    glm::vec3 swirl_translation(kHalfWidth + sin(animation_progress * 0.8) * kHalfWidth * 1.1,
                                kHalfHeight + sin(animation_progress * 0.6) * kHalfHeight * 1.2,
                                elevation);

    // Compute the translation for kFourCorners mode.
    int quadrant = i % 4;
    glm::vec3 quadrant_translation;
    if (quadrant == 0) {
      quadrant_translation = glm::vec3(kHalfWidth * 0.5, kHalfHeight * 0.5, elevation);
    } else if (quadrant == 1) {
      quadrant_translation = glm::vec3(kHalfWidth * 0.5, kHalfHeight * 1.5, elevation);
    } else if (quadrant == 2) {
      quadrant_translation = glm::vec3(kHalfWidth * 1.5, kHalfHeight * 0.5, elevation);
    } else if (quadrant == 3) {
      quadrant_translation = glm::vec3(kHalfWidth * 1.5, kHalfHeight * 1.5, elevation);
    }

    glm::vec3 translation = glm::mix(swirl_translation, quadrant_translation, transition_param);
    float scale = 0.7f + 0.3f * transition_param;

    nodes_[i].SetTranslation(translation.x, translation.y, translation.z);
    nodes_[i].SetScale(scale, scale, scale);
  }
}

bool ViewImpl::PointerDown() {
  if (animation_state_ == kChangingToFourCorners || animation_state_ == kChangingToSwirling) {
    // Ignore input until transition is complete.
    return false;
  }

  switch (animation_state_) {
    case kFourCorners:
      animation_state_ = kChangingToSwirling;
      transition_start_time_ = zx_clock_get_monotonic();
      break;
    case kSwirling:
      animation_state_ = kChangingToFourCorners;
      transition_start_time_ = zx_clock_get_monotonic();
      break;
    default:
      // This will never happen, because we checked above that we're not in a transitional state.
      FX_NOTREACHED() << "already in transition.";
  }
  return true;
}

float ViewImpl::UpdateTransition(zx_time_t presentation_time) {
  double transition_elapsed_seconds =
      static_cast<double>(presentation_time - transition_start_time_) / 1'000'000'000;
  constexpr double kTransitionDuration = 0.5;

  float transition_param = static_cast<float>(transition_elapsed_seconds / kTransitionDuration);

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

void ViewImpl::QuitLoop() {
  async_loop_quit(async_loop_from_dispatcher(async_get_default_dispatcher()));
}

ShadertoyClientView::ShadertoyClientView(scenic::ViewContext context, const std::string& debug_name)
    : scenic::BaseView(std::move(context), debug_name),
      impl_(component_context(), session(), &root_node()) {
  InvalidateScene();
}

void ShadertoyClientView::OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) {
  if (!has_logical_size())
    return;

  impl_.OnSceneInvalidated(std::move(presentation_info), {logical_size().x, logical_size().y});

  InvalidateScene();
}

void ShadertoyClientView::OnPropertiesChanged(fuchsia::ui::gfx::ViewProperties old_properties) {
  InvalidateScene();
}

void ShadertoyClientView::OnMetricsChanged(fuchsia::ui::gfx::Metrics old_metrics) {
  InvalidateScene();
}

void ShadertoyClientView::OnInputEvent(fuchsia::ui::input::InputEvent event) {
  switch (event.Which()) {
    case ::fuchsia::ui::input::InputEvent::Tag::kFocus: {
      focused_ = event.focus().focused;
      break;
    }
    case ::fuchsia::ui::input::InputEvent::Tag::kPointer: {
      const auto& pointer = event.pointer();
      switch (pointer.phase) {
        case ::fuchsia::ui::input::PointerEventPhase::DOWN: {
          if (focused_) {
            impl_.PointerDown();
          }
          break;
        }
        default:
          break;  // Ignore all other pointer phases.
      }
      break;
    }
    case ::fuchsia::ui::input::InputEvent::Tag::kKeyboard:
      break;
    case ::fuchsia::ui::input::InputEvent::Tag::Invalid:
      break;
  }
}

void ShadertoyClientView::OnScenicError(std::string error) {
  FX_LOGS(ERROR) << "Received Scenic Session error: " << error;
}

}  // namespace shadertoy_client
