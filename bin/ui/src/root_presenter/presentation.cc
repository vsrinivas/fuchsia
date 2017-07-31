// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/root_presenter/presentation.h"

#include "application/lib/app/connect.h"
#include "apps/mozart/services/input/cpp/formatting.h"
#include "apps/mozart/services/views/cpp/formatting.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"

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
                           mozart2::SceneManager* scene_manager)
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
      tree_listener_binding_(this),
      tree_container_listener_binding_(this),
      view_container_listener_binding_(this),
      view_listener_binding_(this),
      weak_factory_(this) {
  session_.set_connection_error_handler([this] {
    FTL_LOG(ERROR)
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

void Presentation::Present(mozart::ViewOwnerPtr view_owner,
                           ftl::Closure shutdown_callback) {
  FTL_DCHECK(view_owner);
  FTL_DCHECK(!display_info_);

  shutdown_callback_ = std::move(shutdown_callback);

  scene_manager_->GetDisplayInfo(ftl::MakeCopyable([
    weak = weak_factory_.GetWeakPtr(), view_owner = std::move(view_owner)
  ](mozart2::DisplayInfoPtr display_info) mutable {
    if (weak)
      weak->CreateViewTree(std::move(view_owner), std::move(display_info));
  }));
}

void Presentation::CreateViewTree(mozart::ViewOwnerPtr view_owner,
                                  mozart2::DisplayInfoPtr display_info) {
  FTL_DCHECK(!display_info_);
  FTL_DCHECK(display_info);
  display_info_ = std::move(display_info);
  logical_width_ =
      display_info_->physical_width / display_info_->device_pixel_ratio;
  logical_height_ =
      display_info_->physical_height / display_info_->device_pixel_ratio;
  scene_.SetScale(display_info_->device_pixel_ratio,
                  display_info_->device_pixel_ratio, 1.f);

  layer_.SetSize(static_cast<float>(display_info_->physical_width),
                 static_cast<float>(display_info_->physical_height));

  // Register the view tree.
  mozart::ViewTreeListenerPtr tree_listener;
  tree_listener_binding_.Bind(tree_listener.NewRequest());
  view_manager_->CreateViewTree(tree_.NewRequest(), std::move(tree_listener),
                                "Presentation");
  tree_.set_connection_error_handler([this] {
    FTL_LOG(ERROR) << "Root presenter: View tree connection error.";
    Shutdown();
  });

  // Prepare the view container for the root.
  tree_->GetContainer(tree_container_.NewRequest());
  tree_container_.set_connection_error_handler([this] {
    FTL_LOG(ERROR) << "Root presenter: Tree view container connection error.";
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
    FTL_LOG(WARNING)
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
  root_properties->display_metrics->device_pixel_ratio =
      display_info_->device_pixel_ratio;
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
  FTL_VLOG(1) << "OnDeviceAdded: device_id=" << input_device->id();

  FTL_DCHECK(device_states_by_id_.count(input_device->id()) == 0);
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
  FTL_VLOG(1) << "OnDeviceRemoved: device_id=" << device_id;
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
  FTL_VLOG(2) << "OnReport device=" << device_id
              << ", count=" << device_states_by_id_.count(device_id)
              << ", report=" << *(input_report);

  if (device_states_by_id_.count(device_id) == 0) {
    FTL_VLOG(1) << "OnReport: Unknown device " << device_id;
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
  FTL_VLOG(1) << "OnEvent " << *(event);

  bool invalidate = false;
  if (event->is_pointer()) {
    const mozart::PointerEventPtr& pointer = event->get_pointer();
    if (pointer->type == mozart::PointerEvent::Type::MOUSE) {
      if (cursors_.count(pointer->device_id) == 0) {
        cursors_.emplace(pointer->device_id, CursorState{});
      }

      cursors_[pointer->device_id].position.x = pointer->x;
      cursors_[pointer->device_id].position.y = pointer->y;

      // TODO(jpoichet) for now don't show cursor when mouse is added until we
      // have a timer to hide it. Acer12 sleeve reports 2 mice but only one will
      // generate events for now.
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
  }

  if (invalidate) {
    PresentScene();
  }

  if (input_dispatcher_)
    input_dispatcher_->DispatchEvent(std::move(event));
}

void Presentation::OnChildAttached(uint32_t child_key,
                                   mozart::ViewInfoPtr child_view_info,
                                   const OnChildAttachedCallback& callback) {
  FTL_DCHECK(child_view_info);

  if (kContentViewKey == child_key) {
    FTL_VLOG(1) << "OnChildAttached(content): child_view_info="
                << child_view_info;
  }
  callback();
}

void Presentation::OnChildUnavailable(
    uint32_t child_key,
    const OnChildUnavailableCallback& callback) {
  if (kRootViewKey == child_key) {
    FTL_LOG(ERROR) << "Root presenter: Root view terminated unexpectedly.";
    Shutdown();
  } else if (kContentViewKey == child_key) {
    FTL_LOG(ERROR) << "Root presenter: Content view terminated unexpectedly.";
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

void Presentation::PresentScene() {
  for (auto it = cursors_.begin(); it != cursors_.end(); ++it) {
    CursorState& state = it->second;
    if (state.visible) {
      if (!state.created) {
        state.node = std::make_unique<mozart::client::ShapeNode>(&session_);
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

  session_.Present(0, [](mozart2::PresentationInfoPtr info) {});
}

void Presentation::Shutdown() {
  shutdown_callback_();
}

}  // namespace root_presenter
