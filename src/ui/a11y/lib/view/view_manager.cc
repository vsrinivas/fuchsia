// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/view/view_manager.h"

#include <lib/async/default.h>
#include <lib/fpromise/bridge.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "src/ui/a11y/lib/semantics/semantics_event.h"
#include "src/ui/a11y/lib/util/util.h"

namespace a11y {

ViewManager::ViewManager(std::unique_ptr<SemanticTreeServiceFactory> factory,
                         std::unique_ptr<ViewSemanticsFactory> view_semantics_factory,
                         std::unique_ptr<AnnotationViewFactoryInterface> annotation_view_factory,
                         std::unique_ptr<ViewInjectorFactoryInterface> view_injector_factory,
                         std::unique_ptr<SemanticsEventManager> semantics_event_manager,
                         std::shared_ptr<AccessibilityViewInterface> a11y_view,
                         sys::ComponentContext* context)
    : factory_(std::move(factory)),
      view_semantics_factory_(std::move(view_semantics_factory)),
      annotation_view_factory_(std::move(annotation_view_factory)),
      view_injector_factory_(std::move(view_injector_factory)),
      semantics_event_manager_(std::move(semantics_event_manager)),
      a11y_view_(std::move(a11y_view)),
      virtualkeyboard_listener_binding_(this),
      context_(context) {}

ViewManager::~ViewManager() {
  for (auto& iterator : wait_map_) {
    iterator.second->Cancel();
  }
  wait_map_.clear();
  view_wrapper_map_.clear();
}

void ViewManager::RegisterViewForSemantics(
    fuchsia::ui::views::ViewRef view_ref,
    fidl::InterfaceHandle<fuchsia::accessibility::semantics::SemanticListener> handle,
    fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree> semantic_tree_request) {
  // Clients should register every view that gets created irrespective of the
  // state(enabled/disabled) of screen reader.
  // TODO(fxbug.dev/36199): Check if ViewRef is Valid.
  // TODO(fxbug.dev/36199): When ViewRef is no longer valid, then all the holders of ViewRef will
  // get a signal, and Semantics Manager should then delete the binding for that ViewRef.

  zx_koid_t koid = GetKoid(view_ref);

  auto close_channel_callback = [this, koid](zx_status_t status) {
    if (auto it = view_wrapper_map_.find(koid); it != view_wrapper_map_.end()) {
      auto view_semantics = it->second->view_semantics();
      if (view_semantics) {
        FX_LOGS(INFO) << "View Manager is closing channel with koid:" << koid;
        view_semantics->CloseChannel(status);
      }
    }
    wait_map_.erase(koid);
    view_wrapper_map_.erase(koid);
  };

  auto semantics_event_callback = [this, koid](SemanticsEventInfo event_info) {
    event_info.view_ref_koid = koid;
    if (semantics_event_manager_) {
      semantics_event_manager_->OnEvent(std::move(event_info));
    }
  };

  fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener = handle.Bind();
  semantic_listener.set_error_handler([koid](zx_status_t status) {
    FX_LOGS(INFO) << "Semantic Provider for view with koid " << koid
                  << " disconnected with status: " << zx_status_get_string(status);
  });

  auto service =
      factory_->NewService(koid, std::move(semantic_listener), std::move(close_channel_callback),
                           std::move(semantics_event_callback));

  // As part of the registration, client should get notified about the current Semantics Manager
  // enable settings.
  service->EnableSemanticsUpdates(semantics_enabled_);

  // Start listening for signals on the view ref so that we can clean up associated state.
  auto wait_ptr = std::make_unique<async::WaitMethod<ViewManager, &ViewManager::ViewSignalHandler>>(
      this, view_ref.reference.get(), ZX_EVENTPAIR_PEER_CLOSED);
  FX_CHECK(wait_ptr->Begin(async_get_default_dispatcher()) == ZX_OK);
  wait_map_[koid] = std::move(wait_ptr);
  auto view_semantics = view_semantics_factory_->CreateViewSemantics(
      std::move(service), std::move(semantic_tree_request));
  auto annotation_view = annotation_view_factory_->CreateAndInitAnnotationView(
      fidl::Clone(view_ref), context_,
      // TODO: add callbacks
      []() {}, []() {}, []() {});

  view_wrapper_map_[koid] = std::make_unique<ViewWrapper>(
      std::move(view_ref), std::move(view_semantics), std::move(annotation_view));
}

void ViewManager::Register(
    fuchsia::ui::views::ViewRef view_ref, bool is_visible,
    fidl::InterfaceRequest<fuchsia::accessibility::virtualkeyboard::Listener> listener) {
  zx_koid_t koid = GetKoid(view_ref);

  if (virtualkeyboard_listener_binding_.is_bound() || !ViewHasSemantics(koid)) {
    // In order to dispose of 'listener', as requested in the API, we need to finish the connection
    // and then close it right away. If we don't do that here, the client may wait forever for the
    // connection to finish.
    fidl::Binding<fuchsia::accessibility::virtualkeyboard::Listener> temp(this,
                                                                          std::move(listener));
    temp.Close(ZX_ERR_PEER_CLOSED);
    return;
  }

  // Note that we don't need to listen for the signals of this ViewRef here, since we are already
  // listening for them when the view started providing semantics.
  virtualkeyboard_listener_binding_.Bind(std::move(listener));
  virtualkeyboard_visibility_ = {koid, is_visible};
}

void ViewManager::OnVisibilityChanged(bool updated_visibility,
                                      OnVisibilityChangedCallback callback) {
  virtualkeyboard_visibility_.second = updated_visibility;
  callback();
}

const fxl::WeakPtr<::a11y::SemanticTree> ViewManager::GetTreeByKoid(const zx_koid_t koid) const {
  auto it = view_wrapper_map_.find(koid);
  if (it == view_wrapper_map_.end()) {
    return nullptr;
  }

  auto* view_semantics = it->second->view_semantics();
  return view_semantics == nullptr ? nullptr : view_semantics->GetTree();
}

void ViewManager::SetSemanticsEnabled(bool enabled) {
  semantics_enabled_ = enabled;
  // Notify all the Views about change in Semantics Enabled.
  for (auto& view_wrapper : view_wrapper_map_) {
    auto view_semantics = view_wrapper.second->view_semantics();
    if (view_semantics) {
      view_semantics->EnableSemanticUpdates(enabled);
    }
  }
}

void ViewManager::ViewSignalHandler(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                    zx_status_t status, const zx_packet_signal* signal) {
  zx_koid_t koid = fsl::GetKoid(wait->object());
  wait_map_.erase(koid);
  view_wrapper_map_.erase(koid);
}

bool ViewManager::ViewHasSemantics(zx_koid_t view_ref_koid) {
  auto it = view_wrapper_map_.find(view_ref_koid);
  return it != view_wrapper_map_.end();
}

std::optional<fuchsia::ui::views::ViewRef> ViewManager::ViewRefClone(zx_koid_t view_ref_koid) {
  auto it = view_wrapper_map_.find(view_ref_koid);
  if (it != view_wrapper_map_.end()) {
    return it->second->ViewRefClone();
  }
  return std::nullopt;
}

const fuchsia::accessibility::semantics::Node* ViewManager::GetSemanticNode(
    zx_koid_t koid, uint32_t node_id) const {
  auto tree_weak_ptr = GetTreeByKoid(koid);

  if (!tree_weak_ptr) {
    return nullptr;
  }

  return tree_weak_ptr->GetNode(node_id);
}

const fuchsia::accessibility::semantics::Node* ViewManager::GetParentNode(zx_koid_t koid,
                                                                          uint32_t node_id) const {
  auto tree_weak_ptr = GetTreeByKoid(koid);

  if (!tree_weak_ptr) {
    FX_LOGS(WARNING) << "ViewManager::GetSemanticNode: No semantic tree found for koid: " << koid;
    return nullptr;
  }

  return tree_weak_ptr->GetParentNode(node_id);
}

const fuchsia::accessibility::semantics::Node* ViewManager::GetNextNode(
    zx_koid_t koid, uint32_t node_id, a11y::NodeFilter filter) const {
  auto tree_weak_ptr = GetTreeByKoid(koid);

  if (!tree_weak_ptr) {
    FX_LOGS(WARNING) << "ViewManager::GetSemanticNode: No semantic tree found for koid: " << koid;
    return nullptr;
  }

  return tree_weak_ptr->GetNextNode(node_id, std::move(filter));
}

const fuchsia::accessibility::semantics::Node* ViewManager::GetNextNode(
    zx_koid_t koid, uint32_t node_id, a11y::NodeFilterWithParent filter) const {
  auto tree_weak_ptr = GetTreeByKoid(koid);

  if (!tree_weak_ptr) {
    FX_LOGS(WARNING) << "ViewManager::GetSemanticNode: No semantic tree found for koid: " << koid;
    return nullptr;
  }

  return tree_weak_ptr->GetNextNode(node_id, std::move(filter));
}

const fuchsia::accessibility::semantics::Node* ViewManager::GetPreviousNode(
    zx_koid_t koid, uint32_t node_id, a11y::NodeFilter filter) const {
  auto tree_weak_ptr = GetTreeByKoid(koid);

  if (!tree_weak_ptr) {
    FX_LOGS(WARNING) << "ViewManager::GetSemanticNode: No semantic tree found for koid: " << koid;
    return nullptr;
  }

  return tree_weak_ptr->GetPreviousNode(node_id, std::move(filter));
}

const fuchsia::accessibility::semantics::Node* ViewManager::GetPreviousNode(
    zx_koid_t koid, uint32_t node_id, a11y::NodeFilterWithParent filter) const {
  auto tree_weak_ptr = GetTreeByKoid(koid);

  if (!tree_weak_ptr) {
    FX_LOGS(WARNING) << "ViewManager::GetSemanticNode: No semantic tree found for koid: " << koid;
    return nullptr;
  }

  return tree_weak_ptr->GetPreviousNode(node_id, std::move(filter));
}

bool ViewManager::ViewHasVisibleVirtualkeyboard(zx_koid_t view_ref_koid) {
  return view_ref_koid == virtualkeyboard_visibility_.first && virtualkeyboard_visibility_.second;
}

std::optional<zx_koid_t> ViewManager::GetViewWithVisibleVirtualkeyboard() {
  if (virtualkeyboard_visibility_.second) {
    return virtualkeyboard_visibility_.first;
  }
  return std::nullopt;
}

void ViewManager::ExecuteHitTesting(
    zx_koid_t koid, fuchsia::math::PointF local_point,
    fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback) {
  auto tree_weak_ptr = GetTreeByKoid(koid);

  if (!tree_weak_ptr) {
    FX_LOGS(WARNING) << "ViewManager::ExecuteHitTesting: No semantic tree found for koid: " << koid;
    return;
  }

  tree_weak_ptr->PerformHitTesting(local_point, std::move(callback));
}

void ViewManager::PerformAccessibilityAction(
    zx_koid_t koid, uint32_t node_id, fuchsia::accessibility::semantics::Action action,
    fuchsia::accessibility::semantics::SemanticListener::OnAccessibilityActionRequestedCallback
        callback) {
  auto tree_weak_ptr = GetTreeByKoid(koid);

  if (!tree_weak_ptr) {
    FX_LOGS(WARNING) << "ViewManager::PerformAccessibilityAction: No semantic tree found for koid: "
                     << koid;
    callback(false);
    return;
  }

  tree_weak_ptr->PerformAccessibilityAction(node_id, action, std::move(callback));
}

std::optional<SemanticTransform> ViewManager::GetNodeToRootTransform(zx_koid_t koid,
                                                                     uint32_t node_id) const {
  auto tree_weak_ptr = GetTreeByKoid(koid);

  if (!tree_weak_ptr) {
    FX_LOGS(WARNING) << "Unable to retrieve node-to-root transform for node " << node_id
                     << " in view " << koid << ": tree not found";
  }

  return tree_weak_ptr->GetNodeToRootTransform(node_id);
}

bool ViewManager::InjectEventIntoView(fuchsia::ui::input::InputEvent& event, zx_koid_t koid) {
  auto it = view_wrapper_map_.find(koid);
  if (it == view_wrapper_map_.end()) {
    return false;
  }
  auto* injector = it->second->view_injector();
  if (!injector) {
    return false;
  }

  // In order to inject an event that contains coordinates targeting |koid|, we need to convert them
  // into accessibility view's space coordinates.
  if (!view_coordinate_converter_) {
    return false;
  }
  auto a11y_view_coordinate =
      view_coordinate_converter_->Convert(koid, {event.pointer().x, event.pointer().y});
  if (!a11y_view_coordinate) {
    return false;
  }

  event.pointer().x = a11y_view_coordinate->x;
  event.pointer().y = a11y_view_coordinate->y;

  injector->OnEvent(event);
  return true;
}

bool ViewManager::MarkViewReadyForInjection(zx_koid_t koid, bool ready) {
  auto it = view_wrapper_map_.find(koid);
  if (it == view_wrapper_map_.end()) {
    return false;
  }
  const bool has_injector = it->second->view_injector();

  if (has_injector == ready) {
    // No change of status.
    return true;
  }

  if (!ready) {
    it->second->take_view_injector();
    return true;
  }

  // Instantiates a new injector.
  auto context_view = a11y_view_->view_ref();
  if (!context_view) {
    return false;
  }

  auto target_view = it->second->ViewRefClone();
  auto view_injector = view_injector_factory_->BuildAndConfigureInjector(
      a11y_view_.get(), context_, std::move(*context_view), std::move(target_view));
  it->second->set_view_injector(std::move(view_injector));
  return true;
}

fxl::WeakPtr<ViewWrapper> ViewManager::GetViewWrapper(zx_koid_t view_ref_koid) {
  auto it = view_wrapper_map_.find(view_ref_koid);
  if (it == view_wrapper_map_.end()) {
    return nullptr;
  }

  return it->second->GetWeakPtr();
}

}  // namespace a11y
