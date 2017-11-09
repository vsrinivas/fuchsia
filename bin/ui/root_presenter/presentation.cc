// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/root_presenter/presentation.h"

#include <cmath>

#if defined(countof)
// Workaround for compiler error due to Zircon defining countof() as a macro.
// Redefines countof() using GLM_COUNTOF(), which currently provides a more
// sophisticated implementation anyway.
#undef countof
#include <glm/glm.hpp>
#define countof(X) GLM_COUNTOF(X)
#else
// No workaround required.
#include <glm/glm.hpp>
#endif
#include <glm/ext.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "lib/app/cpp/connect.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/ui/input/cpp/formatting.h"
#include "lib/ui/views/cpp/formatting.h"

namespace root_presenter {
namespace {

constexpr float kPi = glm::pi<float>();

// View Key: The presentation's own root view.
constexpr uint32_t kRootViewKey = 1u;

// View Key: The presented content view.
constexpr uint32_t kContentViewKey = 2u;

// The shape and elevation of the cursor.
constexpr float kCursorWidth = 20;
constexpr float kCursorHeight = 20;
constexpr float kCursorRadius = 10;
constexpr float kCursorElevation = 800;

}  // namespace

Presentation::Presentation(mozart::ViewManager* view_manager,
                           scenic::SceneManager* scene_manager)
    : view_manager_(view_manager),
      scene_manager_(scene_manager),
      session_(scene_manager_),
      compositor_(&session_),
      layer_stack_(&session_),
      layer_(&session_),
      renderer_(&session_),
      scene_(&session_),
      camera_(scene_),
      root_view_host_node_(&session_),
      root_view_parent_node_(&session_),
      content_view_host_node_(&session_),
      cursor_shape_(&session_,
                    kCursorWidth,
                    kCursorHeight,
                    0u,
                    kCursorRadius,
                    kCursorRadius,
                    kCursorRadius),
      cursor_material_(&session_),
      presentation_binding_(this),
      tree_listener_binding_(this),
      tree_container_listener_binding_(this),
      view_container_listener_binding_(this),
      view_listener_binding_(this),
      weak_factory_(this) {
  session_.set_connection_error_handler([this] {
    FXL_LOG(ERROR)
        << "Root presenter: Scene manager session died unexpectedly.";
    Shutdown();
  });

  renderer_.SetCamera(camera_);
  scene_.AddChild(root_view_host_node_);

  layer_.SetRenderer(renderer_);
  layer_stack_.AddLayer(layer_);
  compositor_.SetLayerStack(layer_stack_);

  root_view_host_node_.ExportAsRequest(&root_view_host_import_token_);
  root_view_parent_node_.BindAsRequest(&root_view_parent_export_token_);
  root_view_parent_node_.AddChild(content_view_host_node_);
  content_view_host_node_.ExportAsRequest(&content_view_host_import_token_);
  cursor_material_.SetColor(0xff, 0x00, 0xff, 0xff);
}

Presentation::~Presentation() {}

void Presentation::Present(
    mozart::ViewOwnerPtr view_owner,
    fidl::InterfaceRequest<mozart::Presentation> presentation_request,
    fxl::Closure shutdown_callback) {
  FXL_DCHECK(view_owner);
  FXL_DCHECK(!display_info_);

  shutdown_callback_ = std::move(shutdown_callback);

  scene_manager_->GetDisplayInfo(fxl::MakeCopyable(
      [weak = weak_factory_.GetWeakPtr(), view_owner = std::move(view_owner),
       presentation_request = std::move(presentation_request)](
          scenic::DisplayInfoPtr display_info) mutable {
        if (weak)
          weak->CreateViewTree(std::move(view_owner),
                               std::move(presentation_request),
                               std::move(display_info));
      }));
}

void Presentation::CreateViewTree(
    mozart::ViewOwnerPtr view_owner,
    fidl::InterfaceRequest<mozart::Presentation> presentation_request,
    scenic::DisplayInfoPtr display_info) {
  FXL_DCHECK(!display_info_);
  FXL_DCHECK(display_info);

  if (presentation_request) {
    presentation_binding_.Bind(std::move(presentation_request));
  }

  display_info_ = std::move(display_info);
  device_pixel_ratio_ = display_info_->device_pixel_ratio;
  logical_width_ = display_info_->physical_width / device_pixel_ratio_;
  logical_height_ = display_info_->physical_height / device_pixel_ratio_;

  scene_.SetScale(device_pixel_ratio_, device_pixel_ratio_, 1.f);
  layer_.SetSize(static_cast<float>(display_info_->physical_width),
                 static_cast<float>(display_info_->physical_height));

  // Register the view tree.
  mozart::ViewTreeListenerPtr tree_listener;
  tree_listener_binding_.Bind(tree_listener.NewRequest());
  view_manager_->CreateViewTree(tree_.NewRequest(), std::move(tree_listener),
                                "Presentation");
  tree_.set_connection_error_handler([this] {
    FXL_LOG(ERROR) << "Root presenter: View tree connection error.";
    Shutdown();
  });

  // Prepare the view container for the root.
  tree_->GetContainer(tree_container_.NewRequest());
  tree_container_.set_connection_error_handler([this] {
    FXL_LOG(ERROR) << "Root presenter: Tree view container connection error.";
    Shutdown();
  });
  mozart::ViewContainerListenerPtr tree_container_listener;
  tree_container_listener_binding_.Bind(tree_container_listener.NewRequest());
  tree_container_->SetListener(std::move(tree_container_listener));

  // Get view tree services.
  app::ServiceProviderPtr tree_service_provider;
  tree_->GetServiceProvider(tree_service_provider.NewRequest());
  input_dispatcher_ = app::ConnectToService<mozart::InputDispatcher>(
      tree_service_provider.get());
  input_dispatcher_.set_connection_error_handler([this] {
    // This isn't considered a fatal error right now since it is still useful
    // to be able to test a view system that has graphics but no input.
    FXL_LOG(WARNING)
        << "Input dispatcher connection error, input will not work.";
    input_dispatcher_.reset();
  });

  // Create root view.
  fidl::InterfaceHandle<mozart::ViewOwner> root_view_owner;
  auto root_view_owner_request = root_view_owner.NewRequest();
  mozart::ViewListenerPtr root_view_listener;
  view_listener_binding_.Bind(root_view_listener.NewRequest());
  view_manager_->CreateView(
      root_view_.NewRequest(), std::move(root_view_owner_request),
      std::move(root_view_listener), std::move(root_view_parent_export_token_),
      "RootView");
  root_view_->GetContainer(root_container_.NewRequest());

  // Attach root view to view tree.
  tree_container_->AddChild(kRootViewKey, std::move(root_view_owner),
                            std::move(root_view_host_import_token_));
  auto root_properties = mozart::ViewProperties::New();
  root_properties->display_metrics = mozart::DisplayMetrics::New();
  root_properties->display_metrics->device_pixel_ratio = device_pixel_ratio_;
  root_properties->view_layout = mozart::ViewLayout::New();
  root_properties->view_layout->size = mozart::SizeF::New();
  root_properties->view_layout->size->width = logical_width_;
  root_properties->view_layout->size->height = logical_height_;
  root_properties->view_layout->inset = mozart::InsetF::New();
  tree_container_->SetChildProperties(kRootViewKey, std::move(root_properties));

  // Add content view to root view.
  mozart::ViewContainerListenerPtr view_container_listener;
  view_container_listener_binding_.Bind(view_container_listener.NewRequest());
  root_container_->SetListener(std::move(view_container_listener));
  root_container_->AddChild(kContentViewKey, std::move(view_owner),
                            std::move(content_view_host_import_token_));
  root_container_->SetChildProperties(kContentViewKey,
                                      mozart::ViewProperties::New());

  PresentScene();
}

void Presentation::OnDeviceAdded(mozart::InputDeviceImpl* input_device) {
  FXL_VLOG(1) << "OnDeviceAdded: device_id=" << input_device->id();

  FXL_DCHECK(device_states_by_id_.count(input_device->id()) == 0);
  std::unique_ptr<mozart::DeviceState> state =
      std::make_unique<mozart::DeviceState>(
          input_device->id(), input_device->descriptor(),
          [this](mozart::InputEventPtr event) { OnEvent(std::move(event)); });
  mozart::DeviceState* state_ptr = state.get();
  auto device_pair = std::make_pair(input_device, std::move(state));
  state_ptr->OnRegistered();
  device_states_by_id_.emplace(input_device->id(), std::move(device_pair));
}

void Presentation::OnDeviceRemoved(uint32_t device_id) {
  FXL_VLOG(1) << "OnDeviceRemoved: device_id=" << device_id;
  if (device_states_by_id_.count(device_id) != 0) {
    device_states_by_id_[device_id].second->OnUnregistered();
    auto it = cursors_.find(device_id);
    if (it != cursors_.end()) {
      it->second.node->Detach();
      cursors_.erase(it);
      PresentScene();
    }
    device_states_by_id_.erase(device_id);
  }
}

void Presentation::OnReport(uint32_t device_id,
                            mozart::InputReportPtr input_report) {
  FXL_VLOG(2) << "OnReport device=" << device_id
              << ", count=" << device_states_by_id_.count(device_id)
              << ", report=" << *(input_report);

  if (device_states_by_id_.count(device_id) == 0) {
    FXL_VLOG(1) << "OnReport: Unknown device " << device_id;
    return;
  }

  if (!display_info_)
    return;

  mozart::DeviceState* state = device_states_by_id_[device_id].second.get();
  mozart::Size size;
  size.width = logical_width_;
  size.height = logical_height_;
  state->Update(std::move(input_report), size);
}

void Presentation::OnEvent(mozart::InputEventPtr event) {
  FXL_VLOG(1) << "OnEvent " << *(event);

  bool invalidate = false;
  bool dispatch_event = true;

  // First, allow DisplayFlipper to handle event.
  if (dispatch_event) {
    invalidate |= display_flipper_.OnEvent(event, &scene_, display_info_,
                                           &dispatch_event);
  }

  if (dispatch_event) {
    if (event->is_pointer()) {
      const mozart::PointerEventPtr& pointer = event->get_pointer();

      if (pointer->type == mozart::PointerEvent::Type::MOUSE) {
        if (cursors_.count(pointer->device_id) == 0) {
          cursors_.emplace(pointer->device_id, CursorState{});
        }

        cursors_[pointer->device_id].position.x = pointer->x;
        cursors_[pointer->device_id].position.y = pointer->y;

        // TODO(jpoichet) for now don't show cursor when mouse is added until we
        // have a timer to hide it. Acer12 sleeve reports 2 mice but only one
        // will generate events for now.
        if (pointer->phase != mozart::PointerEvent::Phase::ADD &&
            pointer->phase != mozart::PointerEvent::Phase::REMOVE) {
          cursors_[pointer->device_id].visible = true;
        }
        invalidate = true;
      } else {
        for (auto it = cursors_.begin(); it != cursors_.end(); ++it) {
          if (it->second.visible) {
            it->second.visible = false;
            invalidate = true;
          }
        }
      }

      if (animation_state_ == kTrackball) {
        if (pointer->phase == mozart::PointerEvent::Phase::DOWN) {
          // If we're not already panning/rotating the camera, then start, but
          // only if the touch-down is in the bottom 10% of the screen.
          if (!trackball_pointer_down_ && pointer->y > 0.9f * logical_height_) {
            trackball_pointer_down_ = true;
            trackball_device_id_ = pointer->device_id;
            trackball_pointer_id_ = pointer->pointer_id;
            trackball_previous_x_ = pointer->x;
          }
        } else if (pointer->phase == mozart::PointerEvent::Phase::MOVE) {
          // If the moved pointer is the one that is currently panning/rotating
          // the camera, then update the camera position.
          if (trackball_pointer_down_ &&
              trackball_device_id_ == pointer->device_id &&
              trackball_device_id_ == pointer->device_id) {
            float pan_rate = -2.5f / logical_width_;
            float pan_change = pan_rate * (pointer->x - trackball_previous_x_);
            trackball_previous_x_ = pointer->x;

            camera_pan_ += pan_change;
            if (camera_pan_ < -1.f) {
              camera_pan_ = -1.f;
            } else if (camera_pan_ > 1.f) {
              camera_pan_ = 1.f;
            }
          }
        } else if (pointer->phase == mozart::PointerEvent::Phase::UP) {
          // The pointer was released.
          if (trackball_pointer_down_ &&
              trackball_device_id_ == pointer->device_id &&
              trackball_device_id_ == pointer->device_id) {
            trackball_pointer_down_ = false;
          }
        }
      }
    } else if (event->is_keyboard()) {
      // Alt-Backspace cycles through modes.
      const mozart::KeyboardEventPtr& kbd = event->get_keyboard();
      if ((kbd->modifiers & mozart::kModifierAlt) &&
          kbd->phase == mozart::KeyboardEvent::Phase::PRESSED &&
          kbd->code_point == 0 && kbd->hid_usage == 42 &&
          !trackball_pointer_down_) {
        HandleAltBackspace();
        invalidate = true;
      }
    }
  }

  if (invalidate) {
    PresentScene();
  }

  if (dispatch_event && input_dispatcher_)
    input_dispatcher_->DispatchEvent(std::move(event));
}

void Presentation::HandleAltBackspace() {
  switch (animation_state_) {
    case kDefault:
      animation_state_ = kNoClipping;
      renderer_.SetDisableClipping(true);
      break;
    case kNoClipping:
      animation_state_ = kCameraMovingAway;
      break;
    case kTrackball:
      animation_state_ = kCameraReturning;
      break;
    case kCameraMovingAway:
    case kCameraReturning:
      return;
  }

  animation_start_time_ = zx_time_get(ZX_CLOCK_MONOTONIC);
  UpdateAnimation(animation_start_time_);
}

bool Presentation::UpdateAnimation(uint64_t presentation_time) {
  if (animation_state_ == kDefault || animation_state_ == kNoClipping) {
    return false;
  }

  const float half_width = display_info_->physical_width * 0.5f;
  const float half_height = display_info_->physical_height * 0.5f;

  // Always look at the middle of the stage.
  float target[3] = {half_width, half_height, 0};

  glm::vec3 glm_up(0, 0.1, -0.9);
  glm_up = glm::normalize(glm_up);
  float up[3] = {glm_up[0], glm_up[1], glm_up[2]};

  double secs = static_cast<double>(presentation_time - animation_start_time_) /
                1'000'000'000;
  constexpr double kAnimationDuration = 1.3;
  float param = secs / kAnimationDuration;
  if (param >= 1.f) {
    param = 1.f;
    switch (animation_state_) {
      case kDefault:
      case kNoClipping:
        FXL_DCHECK(false);
        return false;
      case kCameraMovingAway:
        animation_state_ = kTrackball;
        break;
      case kCameraReturning: {
        animation_state_ = kDefault;

        // Switch back to ortho view, and re-enable clipping.
        float ortho_eye[3] = {half_width, half_height, 1100.f};
        camera_.SetProjection(ortho_eye, target, up, 0.f);
        renderer_.SetDisableClipping(false);
        return true;
      }
      case kTrackball:
        break;
    }
  }
  if (animation_state_ == kCameraReturning) {
    param = 1.f - param;  // Animating back to regular position.
  }
  param = glm::smoothstep(0.f, 1.f, param);

  // TODO: kOrthoEyeDist and the values in |eye_end| below are somewhat
  // dependent on the screen size, but also the depth of the stage's viewing
  // volume (currently hardcoded in the SceneManager implementation to 1000, and
  // not available outside).  Since this is a demo feature, it seems OK for now.
  constexpr float kOrthoEyeDist = 60000;
  const float fovy = 2.f * atan(half_height / kOrthoEyeDist);
  glm::vec3 eye_start(half_width, half_height, kOrthoEyeDist);

  constexpr float kEyePanRadius = 1.01f * kOrthoEyeDist;
  constexpr float kMaxPanAngle = kPi / 4;
  float eye_end_x =
      sin(camera_pan_ * kMaxPanAngle) * kEyePanRadius + half_width;
  float eye_end_y =
      cos(camera_pan_ * kMaxPanAngle) * kEyePanRadius + half_height;

  glm::vec3 eye_end(eye_end_x, eye_end_y, 0.75f * kOrthoEyeDist);

  glm::vec3 eye_mid = glm::mix(eye_start, eye_end, 0.4f);
  eye_mid.z = 1.5f * kOrthoEyeDist;

  // Quadratic bezier.
  glm::vec3 eye = glm::mix(glm::mix(eye_start, eye_mid, param),
                           glm::mix(eye_mid, eye_end, param), param);

  camera_.SetProjection(glm::value_ptr(eye), target, up, fovy);

  return true;
}

void Presentation::OnChildAttached(uint32_t child_key,
                                   mozart::ViewInfoPtr child_view_info,
                                   const OnChildAttachedCallback& callback) {
  FXL_DCHECK(child_view_info);

  if (kContentViewKey == child_key) {
    FXL_VLOG(1) << "OnChildAttached(content): child_view_info="
                << child_view_info;
  }
  callback();
}

void Presentation::OnChildUnavailable(
    uint32_t child_key,
    const OnChildUnavailableCallback& callback) {
  if (kRootViewKey == child_key) {
    FXL_LOG(ERROR) << "Root presenter: Root view terminated unexpectedly.";
    Shutdown();
  } else if (kContentViewKey == child_key) {
    FXL_LOG(ERROR) << "Root presenter: Content view terminated unexpectedly.";
    Shutdown();
  }
  callback();
}

void Presentation::OnPropertiesChanged(
    mozart::ViewPropertiesPtr properties,
    const OnPropertiesChangedCallback& callback) {
  // Nothing to do right now.
  callback();
}

// |Presentation|
void Presentation::EnableClipping(bool enabled) {
  FXL_LOG(INFO) << "Presentation Controller method called: EnableClipping!!";
}

// |Presentation|
void Presentation::UseOrthographicView() {
  FXL_LOG(INFO)
      << "Presentation Controller method called: UseOrthographicView!!";
}

// |Presentation|
void Presentation::UsePerspectiveView() {
  FXL_LOG(INFO)
      << "Presentation Controller method called: UsePerspectiveView!!";
}

void Presentation::PresentScene() {
  for (auto it = cursors_.begin(); it != cursors_.end(); ++it) {
    CursorState& state = it->second;
    if (state.visible) {
      if (!state.created) {
        state.node = std::make_unique<scenic_lib::ShapeNode>(&session_);
        state.node->SetShape(cursor_shape_);
        state.node->SetMaterial(cursor_material_);
        scene_.AddChild(*state.node);
        state.created = true;
      }
      state.node->SetTranslation(state.position.x + kCursorWidth * .5f,
                                 state.position.y + kCursorHeight * .5f,
                                 kCursorElevation);
    } else if (state.created) {
      state.node->Detach();
      state.created = false;
    }
  }

  session_.Present(
      0, [weak = weak_factory_.GetWeakPtr()](scenic::PresentationInfoPtr info) {
        if (auto self = weak.get()) {
          uint64_t next_presentation_time =
              info->presentation_time + info->presentation_interval;
          if (self->UpdateAnimation(next_presentation_time)) {
            self->PresentScene();
          }
        }
      });
}

void Presentation::Shutdown() {
  shutdown_callback_();
}

void Presentation::SetRendererParams(
    ::fidl::Array<scenic::RendererParamPtr> params) {
  for (auto& param : params) {
    renderer_.SetParam(std::move(param));
  }
  session_.Present(0, [](scenic::PresentationInfoPtr info) {});
}

}  // namespace root_presenter
