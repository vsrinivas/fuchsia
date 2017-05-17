// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/root_presenter/presentation.h"

#include <mx/vmo.h>

#include "application/lib/app/connect.h"
#include "apps/mozart/lib/skia/skia_vmo_data.h"
#include "apps/mozart/lib/skia/skia_vmo_surface.h"
#include "apps/mozart/services/composition/resources.fidl.h"
#include "apps/mozart/services/composition/scenes.fidl.h"
#include "apps/mozart/services/geometry/cpp/geometry_util.h"
#include "apps/mozart/services/input/cpp/formatting.h"
#include "apps/mozart/services/views/cpp/formatting.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/vmo/file.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkData.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace root_presenter {
namespace {

// View Key: The presentation's own root view.
constexpr uint32_t kRootViewKey = 1u;

// View Key: The presented content view.
constexpr uint32_t kContentViewKey = 2u;

// Node Id: The root scene node.
constexpr uint32_t kRootNodeId = mozart::kSceneRootNodeId;

// Node Id: The scene node that draws the presented content.
constexpr uint32_t kContentNodeId = 1u;

// Node Id: The scene node that draws the mouse pointer.
constexpr uint32_t kCursorBaseNodeId = 2u;

// Resource Id: The root scene of the presented view.
constexpr uint32_t kContentSceneResourceId = 1u;

// Resource Id: The cursor image.
constexpr uint32_t kCursorImageResourceId = 2u;

// Path of the cursor image to load.
constexpr char kCursorImage[] = "/system/data/root_presenter/cursor32.png";

}  // namespace

Presentation::Presentation(mozart::Compositor* compositor,
                           mozart::ViewManager* view_manager,
                           mozart::ViewOwnerPtr view_owner)
    : compositor_(compositor),
      view_manager_(view_manager),
      view_owner_(std::move(view_owner)),
      tree_listener_binding_(this),
      tree_container_listener_binding_(this),
      view_container_listener_binding_(this),
      view_listener_binding_(this),
      weak_ptr_factory_(this) {
  FTL_DCHECK(compositor_);
  FTL_DCHECK(view_manager_);
  FTL_DCHECK(view_owner_);
  LoadCursor();
}

Presentation::~Presentation() {}

void Presentation::Present(ftl::Closure shutdown_callback) {
  shutdown_callback_ = std::move(shutdown_callback);

  compositor_->CreateRenderer(renderer_.NewRequest(), "Presentation");
  renderer_.set_connection_error_handler([this] {
    FTL_LOG(ERROR) << "Renderer died unexpectedly.";
    Shutdown();
  });

  renderer_->GetDisplayInfo([this](mozart::DisplayInfoPtr display_info) {
    display_info_ = std::move(display_info);
    CreateViewTree();
  });
}

void Presentation::CreateViewTree() {
  FTL_DCHECK(renderer_);
  FTL_DCHECK(view_owner_);
  FTL_DCHECK(display_info_);

  // Register the view tree.
  mozart::ViewTreeListenerPtr tree_listener;
  tree_listener_binding_.Bind(tree_listener.NewRequest());
  view_manager_->CreateViewTree(tree_.NewRequest(), std::move(tree_listener),
                                "Presentation");
  tree_.set_connection_error_handler([this] {
    FTL_LOG(ERROR) << "View tree connection error.";
    Shutdown();
  });

  // Prepare the view container for the root.
  tree_->GetContainer(tree_container_.NewRequest());
  tree_container_.set_connection_error_handler([this] {
    FTL_LOG(ERROR) << "Tree view container connection error.";
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

  // Attach the renderer.
  tree_->SetRenderer(std::move(renderer_));

  // Create root view
  mozart::ViewListenerPtr root_view_listener;
  view_listener_binding_.Bind(root_view_listener.NewRequest());
  view_manager_->CreateView(root_view_.NewRequest(),
                            root_view_owner_.NewRequest(),
                            std::move(root_view_listener), "RootView");
  tree_container_->AddChild(kRootViewKey, std::move(root_view_owner_));
  root_view_->CreateScene(root_scene_.NewRequest());

  // Add content view to root view
  root_view_->GetContainer(root_container_.NewRequest());

  mozart::ViewContainerListenerPtr view_container_listener;
  view_container_listener_binding_.Bind(view_container_listener.NewRequest());
  root_container_->SetListener(std::move(view_container_listener));

  root_container_->AddChild(kContentViewKey, std::move(view_owner_));

  UpdateRootViewProperties();
}

void Presentation::LoadCursor() {
  // TODO(jpoichet) Since VmoFromFilename copies the file into a VMO we should
  // do this asynchronously
  mx::vmo vmo;
  if (mtl::VmoFromFilename(kCursorImage, &vmo)) {
    sk_sp<SkData> data = mozart::MakeSkDataFromVMO(vmo);
    cursor_image_ = data ? SkImage::MakeFromEncoded(data) : nullptr;
  }
  if (!cursor_image_) {
    FTL_LOG(ERROR) << "Failed to load " << kCursorImage;
  }
}

mozart::SceneMetadataPtr Presentation::CreateSceneMetadata() const {
  auto metadata = mozart::SceneMetadata::New();
  metadata->version = scene_version_;
  metadata->presentation_time = frame_tracker_.frame_info().presentation_time;
  return metadata;
}

// When |initiliaze| is true, resources and nodes are created. On subsequent
// update we only re-create the tree and don't touch resources
void Presentation::UpdateScene() {
  FTL_DCHECK(root_scene_);
  auto update = mozart::SceneUpdate::New();

  if (content_view_info_ && !scene_resources_uploaded_) {
    auto scene_resource = mozart::Resource::New();
    scene_resource->set_scene(mozart::SceneResource::New());
    scene_resource->get_scene()->scene_token =
        content_view_info_->scene_token.Clone();
    update->resources.insert(kContentSceneResourceId,
                             std::move(scene_resource));
    scene_resources_uploaded_ = true;
    layout_changed_ = true;
  }

  if (cursor_image_ && !cursor_resources_uploaded_) {
    mozart::Size size;
    size.width = 32;
    size.height = 32;

    mozart::ImagePtr image;
    sk_sp<SkSurface> surface =
        mozart::MakeSkSurface(size, &buffer_producer_, &image);
    FTL_CHECK(surface);
    SkCanvas* canvas = surface->getCanvas();
    canvas->drawImage(cursor_image_, 0, 0);
    canvas->flush();

    auto cursor_resource = mozart::Resource::New();
    cursor_resource->set_image(mozart::ImageResource::New());
    cursor_resource->get_image()->image = std::move(image);
    update->resources.insert(kCursorImageResourceId,
                             std::move(cursor_resource));
    cursor_resources_uploaded_ = true;
    layout_changed_ = true;
  }

  if (cursor_image_) {
    mozart::RectF cursor;
    cursor.width = 32.f;
    cursor.height = 32.f;
    for (std::map<uint32_t, std::pair<bool, mozart::PointF>>::iterator it =
             cursors_.begin();
         it != cursors_.end(); ++it) {
      if (it->second.first) {  // show cursor
        auto cursor_node = mozart::Node::New();
        cursor_node->op = mozart::NodeOp::New();
        cursor_node->op->set_image(mozart::ImageNodeOp::New());
        cursor_node->op->get_image()->content_rect = cursor.Clone();
        cursor_node->op->get_image()->image_resource_id =
            kCursorImageResourceId;

        cursor_node->content_transform = mozart::Transform::New();
        SetTranslationTransform(cursor_node->content_transform.get(),
                                it->second.second.x, it->second.second.y, 0.f);
        update->nodes.insert(kCursorBaseNodeId + it->first,
                             std::move(cursor_node));
      }
    }
  }

  if (layout_changed_) {
    auto root_node = mozart::Node::New();

    if (content_view_info_) {
      auto content_node = mozart::Node::New();
      content_node->op = mozart::NodeOp::New();
      content_node->op->set_scene(mozart::SceneNodeOp::New());
      content_node->op->get_scene()->scene_resource_id =
          kContentSceneResourceId;
      update->nodes.insert(kContentNodeId, std::move(content_node));

      root_node->child_node_ids.push_back(kContentNodeId);
    }

    if (cursor_image_) {
      for (std::map<uint32_t, std::pair<bool, mozart::PointF>>::iterator it =
               cursors_.begin();
           it != cursors_.end(); ++it) {
        if (it->second.first) {  // show cursor
          root_node->child_node_ids.push_back(kCursorBaseNodeId + it->first);
        }
      }
    }

    update->nodes.insert(kRootNodeId, std::move(root_node));
    layout_changed_ = false;
  }

  root_scene_->Update(std::move(update));
  root_scene_->Publish(CreateSceneMetadata());
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
    if (cursors_.count(device_id)) {
      cursors_.erase(device_id);
      layout_changed_ = true;
      root_view_->Invalidate();
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

  if (!display_info_) {
    FTL_VLOG(1) << "OnReport: No display info " << device_id;
    return;
  }

  mozart::DeviceState* state = device_states_by_id_[device_id].second.get();
  state->Update(std::move(input_report), *display_info_->size);
}

void Presentation::OnEvent(mozart::InputEventPtr event) {
  FTL_VLOG(1) << "OnEvent " << *(event);
  if (event->is_pointer()) {
    const mozart::PointerEventPtr& pointer = event->get_pointer();
    if (pointer->type == mozart::PointerEvent::Type::MOUSE) {
      if (cursors_.count(pointer->device_id) == 0) {
        mozart::PointF position;
        position.x = 0;
        position.y = 0;
        cursors_[pointer->device_id] = std::make_pair(false, position);
      }

      cursors_[pointer->device_id].second.x = pointer->x;
      cursors_[pointer->device_id].second.y = pointer->y;
      // TODO(jpoichet) for now don't show cursor when mouse is added until we
      // have a timer to hide it. Acer12 sleeve reports 2 mice but only one will
      // generate events for now.
      if (!cursors_[pointer->device_id].first &&
          pointer->phase != mozart::PointerEvent::Phase::ADD &&
          pointer->phase != mozart::PointerEvent::Phase::REMOVE) {
        layout_changed_ = true;
        cursors_[pointer->device_id].first = true;
      }
      root_view_->Invalidate();
    } else {
      bool invalidate = false;
      for (std::map<uint32_t, std::pair<bool, mozart::PointF>>::iterator it =
               cursors_.begin();
           it != cursors_.end(); ++it) {
        if (it->second.first) {  // show_cursor
          layout_changed_ = true;
          it->second.first = false;
          invalidate = true;
        }
      }
      if (invalidate) {
        root_view_->Invalidate();
      }
    }
  }

  if (input_dispatcher_)
    input_dispatcher_->DispatchEvent(std::move(event));
}

void Presentation::OnLayout() {
  auto properties = mozart::ViewProperties::New();
  properties->display_metrics = mozart::DisplayMetrics::New();
  properties->display_metrics->device_pixel_ratio =
      display_info_->device_pixel_ratio;
  properties->view_layout = mozart::ViewLayout::New();
  properties->view_layout->size = display_info_->size.Clone();
  properties->view_layout->inset = mozart::Inset::New();

  root_container_->SetChildProperties(
      kContentViewKey, mozart::kSceneVersionNone, std::move(properties));
}

void Presentation::UpdateRootViewProperties() {
  auto properties = mozart::ViewProperties::New();
  properties->display_metrics = mozart::DisplayMetrics::New();
  properties->display_metrics->device_pixel_ratio =
      display_info_->device_pixel_ratio;
  properties->view_layout = mozart::ViewLayout::New();
  properties->view_layout->size = display_info_->size.Clone();
  properties->view_layout->inset = mozart::Inset::New();

  tree_container_->SetChildProperties(kRootViewKey, mozart::kSceneVersionNone,
                                      std::move(properties));
}

void Presentation::OnChildAttached(uint32_t child_key,
                                   mozart::ViewInfoPtr child_view_info,
                                   const OnChildAttachedCallback& callback) {
  FTL_DCHECK(child_view_info);

  if (kContentViewKey == child_key) {
    FTL_VLOG(1) << "OnChildAttached(content): child_view_info="
                << child_view_info;
    content_view_info_ = std::move(child_view_info);
    layout_changed_ = true;
    root_view_->Invalidate();
  }

  callback();
}

void Presentation::OnChildUnavailable(
    uint32_t child_key,
    const OnChildUnavailableCallback& callback) {
  if (kRootViewKey == child_key) {
    FTL_LOG(ERROR) << "Root view terminated unexpectedly.";
    Shutdown();
  } else if (kContentViewKey == child_key) {
    FTL_LOG(ERROR) << "Content view terminated unexpectedly.";
    Shutdown();
  }
  callback();
}

void Presentation::OnInvalidation(mozart::ViewInvalidationPtr invalidation,
                                  const OnInvalidationCallback& callback) {
  frame_tracker_.Update(*invalidation->frame_info, ftl::TimePoint::Now());
  scene_version_ = invalidation->scene_version;
  if (invalidation->properties) {
    OnLayout();
  }
  UpdateScene();
  callback();
}

void Presentation::OnRendererDied(const OnRendererDiedCallback& callback) {
  FTL_LOG(ERROR) << "Renderer died unexpectedly.";
  Shutdown();
  callback();
}

void Presentation::Shutdown() {
  shutdown_callback_();
}

}  // namespace root_presenter
