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

#include "garnet/bin/ui/root_presenter/displays/display_configuration.h"

namespace root_presenter {
namespace {

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
                           ui::Scenic* scenic)
    : view_manager_(view_manager),
      scenic_(scenic),
      session_(scenic_),
      compositor_(&session_),
      layer_stack_(&session_),
      layer_(&session_),
      renderer_(&session_),
      scene_(&session_),
      camera_(scene_),
      ambient_light_(&session_),
      directional_light_(&session_),
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
  session_.set_error_handler([this] {
    FXL_LOG(ERROR)
        << "Root presenter: Scene manager session died unexpectedly.";
    Shutdown();
  });

  renderer_.SetCamera(camera_);
  scene_.AddChild(root_view_host_node_);

  scene_.AddLight(ambient_light_);
  scene_.AddLight(directional_light_);
  ambient_light_.SetColor(0.3f, 0.3f, 0.3f);
  directional_light_.SetColor(0.7f, 0.7f, 0.7f);
  light_direction_ = glm::vec3(1.f, 1.f, -2.f);
  directional_light_.SetDirection(light_direction_.x, light_direction_.y,
                                  light_direction_.z);

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
    f1dl::InterfaceRequest<mozart::Presentation> presentation_request,
    fxl::Closure shutdown_callback) {
  FXL_DCHECK(view_owner);
  FXL_DCHECK(!display_model_initialized_);

  shutdown_callback_ = std::move(shutdown_callback);

  scenic_->GetDisplayInfo(fxl::MakeCopyable(
      [weak = weak_factory_.GetWeakPtr(), view_owner = std::move(view_owner),
       presentation_request = std::move(presentation_request)](
          ui::gfx::DisplayInfoPtr display_info) mutable {
        if (weak)
          weak->CreateViewTree(std::move(view_owner),
                               std::move(presentation_request),
                               std::move(display_info));
      }));
}

void Presentation::CreateViewTree(
    mozart::ViewOwnerPtr view_owner,
    f1dl::InterfaceRequest<mozart::Presentation> presentation_request,
    ui::gfx::DisplayInfoPtr display_info) {
  FXL_DCHECK(display_info);

  if (presentation_request) {
    presentation_binding_.Bind(std::move(presentation_request));
  }

  // Register the view tree.
  mozart::ViewTreeListenerPtr tree_listener;
  tree_listener_binding_.Bind(tree_listener.NewRequest());
  view_manager_->CreateViewTree(tree_.NewRequest(), std::move(tree_listener),
                                "Presentation");
  tree_.set_error_handler([this] {
    FXL_LOG(ERROR) << "Root presenter: View tree connection error.";
    Shutdown();
  });

  // Prepare the view container for the root.
  tree_->GetContainer(tree_container_.NewRequest());
  tree_container_.set_error_handler([this] {
    FXL_LOG(ERROR) << "Root presenter: Tree view container connection error.";
    Shutdown();
  });
  mozart::ViewContainerListenerPtr tree_container_listener;
  tree_container_listener_binding_.Bind(tree_container_listener.NewRequest());
  tree_container_->SetListener(std::move(tree_container_listener));

  // Get view tree services.
  component::ServiceProviderPtr tree_service_provider;
  tree_->GetServiceProvider(tree_service_provider.NewRequest());
  input_dispatcher_ = component::ConnectToService<mozart::InputDispatcher>(
      tree_service_provider.get());
  input_dispatcher_.set_error_handler([this] {
    // This isn't considered a fatal error right now since it is still useful
    // to be able to test a view system that has graphics but no input.
    FXL_LOG(WARNING)
        << "Input dispatcher connection error, input will not work.";
    input_dispatcher_.Unbind();
  });

  // Create root view.
  f1dl::InterfaceHandle<mozart::ViewOwner> root_view_owner;
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

  // Get display parameters and propagate values appropriately.
  InitializeDisplayModel(std::move(display_info));

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

void Presentation::InitializeDisplayModel(
    ui::gfx::DisplayInfoPtr display_info) {
  FXL_DCHECK(!display_model_initialized_);

  // Save previous display values. These could have been overriden by earlier
  // calls to SetDisplayUsage() and SetDisplaySizeInMm(); if not, they will
  // be unknown or 0.
  auto previous_display_usage =
      display_model_simulated_.environment_info().usage;

  auto previous_display_width_in_mm =
      display_model_simulated_.display_info().width_in_mm;
  auto previous_display_height_in_mm =
      display_model_simulated_.display_info().height_in_mm;

  // Initialize display model.
  display_configuration::InitializeModelForDisplay(display_info->width_in_px,
                                                   display_info->height_in_px,
                                                   &display_model_actual_);
  display_model_simulated_ = display_model_actual_;

  display_model_initialized_ = true;

  // Re-set the model with previous values. If they were unknown or 0, the
  // actual/default values will be used.
  SetDisplayUsageWithoutApplyingChanges(previous_display_usage);
  SetDisplaySizeInMmWithoutApplyingChanges(previous_display_width_in_mm,
                                           previous_display_height_in_mm, true);

  ApplyDisplayModelChanges(true);
}

void Presentation::SetDisplaySizeInMm(float width_in_mm, float height_in_mm) {
  uint32_t old_width_in_mm =
      display_model_simulated_.display_info().width_in_mm;
  uint32_t old_height_in_mm =
      display_model_simulated_.display_info().height_in_mm;

  SetDisplaySizeInMmWithoutApplyingChanges(width_in_mm, height_in_mm, true);

  if (display_model_simulated_.display_info().width_in_mm == old_width_in_mm &&
      display_model_simulated_.display_info().height_in_mm ==
          old_height_in_mm) {
    // Nothing needs to be changed.
    return;
  }

  FXL_LOG(INFO) << "Presentation::SetDisplaySizeInMm: changing display "
                   "dimensions to "
                << "width="
                << display_model_simulated_.display_info().width_in_mm << "mm, "
                << "height="
                << display_model_simulated_.display_info().height_in_mm
                << "mm.";

  ApplyDisplayModelChanges(true);
}

bool Presentation::SetDisplaySizeInMmWithoutApplyingChanges(float width_in_mm,
                                                            float height_in_mm,
                                                            bool print_errors) {
  if (width_in_mm == 0 || height_in_mm == 0) {
    display_model_simulated_.display_info().width_in_px =
        display_model_actual_.display_info().width_in_px;
    display_model_simulated_.display_info().height_in_px =
        display_model_actual_.display_info().height_in_px;
    display_model_simulated_.display_info().width_in_mm =
        display_model_actual_.display_info().width_in_mm;
    display_model_simulated_.display_info().height_in_mm =
        display_model_actual_.display_info().height_in_mm;
    return true;
  }

  const float kPxPerMm =
      display_model_actual_.display_info().density_in_px_per_mm;
  uint32_t width_in_px = width_in_mm * kPxPerMm;
  uint32_t height_in_px = height_in_mm * kPxPerMm;

  if (width_in_px > display_model_actual_.display_info().width_in_px) {
    if (print_errors) {
      FXL_LOG(ERROR) << "Presentation::SetDisplaySizeInMm: tried to change "
                        "display width to "
                     << width_in_mm
                     << ", which is larger than the actual display width "
                     << display_model_actual_.display_info().width_in_px /
                            kPxPerMm;
    }
    return false;
  }
  if (height_in_px > display_model_actual_.display_info().height_in_px) {
    if (print_errors) {
      FXL_LOG(ERROR) << "Presentation::SetDisplaySizeInMm: tried to change "
                        "display height to "
                     << height_in_mm
                     << ", which is larger than the actual display height "
                     << display_model_actual_.display_info().height_in_px /
                            kPxPerMm;
    }
    return false;
  }

  display_model_simulated_.display_info().width_in_px = width_in_px;
  display_model_simulated_.display_info().height_in_px = height_in_px;
  display_model_simulated_.display_info().width_in_mm = width_in_mm;
  display_model_simulated_.display_info().height_in_mm = height_in_mm;
  return true;
}

void Presentation::SetDisplayUsage(mozart::DisplayUsage usage) {
  mozart::DisplayUsage old_usage =
      display_model_simulated_.environment_info().usage;
  SetDisplayUsageWithoutApplyingChanges(usage);
  if (display_model_simulated_.environment_info().usage == old_usage) {
    // Nothing needs to be changed.
    return;
  }

  ApplyDisplayModelChanges(true);

  FXL_LOG(INFO) << "Presentation::SetDisplayUsage: changing display usage to "
                << GetDisplayUsageAsString(
                       display_model_simulated_.environment_info().usage);
}

void Presentation::SetDisplayUsageWithoutApplyingChanges(
    mozart::DisplayUsage usage) {
  display_model_simulated_.environment_info().usage =
      (usage == mozart::DisplayUsage::UNKNOWN)
          ? display_model_actual_.environment_info().usage
          : usage;
}

bool Presentation::ApplyDisplayModelChanges(bool print_log) {
  if (!display_model_initialized_)
    return false;

  DisplayMetrics metrics = display_model_simulated_.GetMetrics();

  if (print_log) {
    display_configuration::LogDisplayMetrics(metrics);
  }

  if (display_metrics_ == metrics)
    return true;

  display_metrics_ = metrics;

  auto root_properties = mozart::ViewProperties::New();
  root_properties->display_metrics = mozart::DisplayMetrics::New();

  // TODO(MZ-411): Handle densities that differ in x and y.
  FXL_DCHECK(display_metrics_.x_scale_in_px_per_pp() ==
             display_metrics_.y_scale_in_px_per_pp());
  root_properties->display_metrics->device_pixel_ratio =
      display_metrics_.x_scale_in_px_per_pp();
  root_properties->view_layout = mozart::ViewLayout::New();
  root_properties->view_layout->size = mozart::SizeF::New();
  root_properties->view_layout->size->width = display_metrics_.width_in_pp();
  root_properties->view_layout->size->height = display_metrics_.height_in_pp();
  root_properties->view_layout->inset = mozart::InsetF::New();
  tree_container_->SetChildProperties(kRootViewKey, std::move(root_properties));

  // Apply device pixel ratio.
  scene_.SetScale(display_metrics_.x_scale_in_px_per_pp(),
                  display_metrics_.y_scale_in_px_per_pp(), 1.f);

  // Center everything.
  float left_offset = (display_model_actual_.display_info().width_in_px -
                       display_metrics_.width_in_px()) /
                      2;
  float top_offset = (display_model_actual_.display_info().height_in_px -
                      display_metrics_.height_in_px()) /
                     2;
  root_view_host_node_.SetTranslation(
      left_offset / display_metrics_.x_scale_in_px_per_pp(),
      top_offset / display_metrics_.y_scale_in_px_per_pp(), 0.f);

  layer_.SetSize(
      static_cast<float>(display_model_actual_.display_info().width_in_px),
      static_cast<float>(display_model_actual_.display_info().height_in_px));
  return true;
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

  if (!display_model_initialized_)
    return;

  mozart::DeviceState* state = device_states_by_id_[device_id].second.get();
  mozart::Size size;
  size.width = display_model_actual_.display_info().width_in_px;
  size.height = display_model_actual_.display_info().height_in_px;
  state->Update(std::move(input_report), size);
}

void Presentation::CaptureKeyboardEvent(
    mozart::KeyboardEventPtr event_to_capture,
    f1dl::InterfaceHandle<mozart::KeyboardCaptureListener> listener_handle) {
  mozart::KeyboardCaptureListenerPtr listener;
  listener.Bind(std::move(listener_handle));
  // Auto-remove listeners if the interface closes.
  listener.set_error_handler([ this, listener = listener.get() ] {
    captured_keybindings_.erase(
        std::remove_if(captured_keybindings_.begin(),
                       captured_keybindings_.end(),
                       [listener](const KeyboardCaptureItem& item) -> bool {
                         return item.listener.get() == listener;
                       }),
        captured_keybindings_.end());
  });

  captured_keybindings_.push_back(
      KeyboardCaptureItem{std::move(event_to_capture), std::move(listener)});
}

bool Presentation::GlobalHooksHandleEvent(const mozart::InputEventPtr& event) {
  return display_flipper_.OnEvent(event, this) ||
         display_usage_switcher_.OnEvent(event, this) ||
         display_size_switcher_.OnEvent(event, this) ||
         perspective_demo_mode_.OnEvent(event, this);
}

void Presentation::OnEvent(mozart::InputEventPtr event) {
  FXL_VLOG(1) << "OnEvent " << *(event);

  bool invalidate = false;
  bool dispatch_event = true;

  if (GlobalHooksHandleEvent(event)) {
    invalidate = true;
    dispatch_event = false;
  }

  // Process the event.
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

    } else if (event->is_keyboard()) {
      const mozart::KeyboardEventPtr& kbd = event->get_keyboard();

      for (size_t i = 0; i < captured_keybindings_.size(); i++) {
        const auto& event = captured_keybindings_[i].event;
        if ((kbd->modifiers & event->modifiers) &&
            (event->phase == kbd->phase) &&
            (event->code_point == kbd->code_point)) {
          captured_keybindings_[i].listener->OnEvent(kbd.Clone());
        }
      }
    }
  }

  if (invalidate) {
    PresentScene();
  }

  if (dispatch_event && input_dispatcher_)
    input_dispatcher_->DispatchEvent(std::move(event));
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
      state.node->SetTranslation(
          state.position.x * display_metrics_.x_scale_in_pp_per_px() +
              kCursorWidth * .5f,
          state.position.y * display_metrics_.y_scale_in_pp_per_px() +
              kCursorHeight * .5f,
          kCursorElevation);
    } else if (state.created) {
      state.node->Detach();
      state.created = false;
    }
  }

  session_.Present(
      0, [weak = weak_factory_.GetWeakPtr()](ui::PresentationInfoPtr info) {
        if (auto self = weak.get()) {
          uint64_t next_presentation_time =
              info->presentation_time + info->presentation_interval;
          if (self->perspective_demo_mode_.UpdateAnimation(
                  self, next_presentation_time)) {
            self->PresentScene();
          }
        }
      });
}

void Presentation::Shutdown() {
  shutdown_callback_();
}

void Presentation::SetRendererParams(
    ::f1dl::VectorPtr<ui::gfx::RendererParamPtr> params) {
  for (size_t i = 0; i < params->size(); ++i) {
    renderer_.SetParam(std::move(params->at(i)));
  }
  session_.Present(0, [](ui::PresentationInfoPtr info) {});
}

}  // namespace root_presenter
