// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/view/view_manager.h"

#include <lib/async/default.h>
#include <lib/fit/bridge.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "src/ui/a11y/lib/semantics/semantics_event.h"
#include "src/ui/a11y/lib/util/util.h"

namespace a11y {

ViewManager::ViewManager(std::unique_ptr<SemanticTreeServiceFactory> factory,
                         std::unique_ptr<ViewSemanticsFactory> view_semantics_factory,
                         std::unique_ptr<AnnotationViewFactoryInterface> annotation_view_factory,
                         std::unique_ptr<SemanticsEventManager> semantics_event_manager,
                         sys::ComponentContext* context, vfs::PseudoDir* debug_dir)
    : factory_(std::move(factory)),
      view_semantics_factory_(std::move(view_semantics_factory)),
      annotation_view_factory_(std::move(annotation_view_factory)),
      semantics_event_manager_(std::move(semantics_event_manager)),
      context_(context),
      debug_dir_(debug_dir) {}

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
      FX_LOGS(INFO) << "View Manager is closing channel with koid:" << koid;
      it->second->CloseChannel(status);
    }
    wait_map_.erase(koid);
    view_wrapper_map_.erase(koid);
  };

  auto semantics_event_callback = [this, koid](SemanticsEventType event_type) {
    SemanticsEventInfo event;
    event.event_type = event_type;
    event.view_ref_koid = koid;
    if (semantics_event_manager_) {
      semantics_event_manager_->OnEvent(event);
    }
  };

  fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener = handle.Bind();
  semantic_listener.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Semantic Provider disconnected with status: "
                   << zx_status_get_string(status);
  });

  auto service =
      factory_->NewService(koid, std::move(semantic_listener), debug_dir_,
                           std::move(close_channel_callback), std::move(semantics_event_callback));

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

const fxl::WeakPtr<::a11y::SemanticTree> ViewManager::GetTreeByKoid(const zx_koid_t koid) const {
  auto it = view_wrapper_map_.find(koid);
  return it != view_wrapper_map_.end() ? it->second->GetTree() : nullptr;
}

void ViewManager::SetSemanticsEnabled(bool enabled) {
  semantics_enabled_ = enabled;
  // Notify all the Views about change in Semantics Enabled.
  for (auto& view_wrapper : view_wrapper_map_) {
    view_wrapper.second->EnableSemanticUpdates(enabled);
  }
}

void ViewManager::SetAnnotationsEnabled(bool annotations_enabled) {
  // This function call should be a noop if annotation state is not changing.
  if (annotations_enabled_ == annotations_enabled) {
    return;
  }

  // If we are disabling annotations, then we should clear the existing
  // highlight (if any).
  if (!annotations_enabled) {
    ClearHighlight();
  }

  annotations_enabled_ = annotations_enabled;
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
    FX_LOGS(ERROR) << "ViewManager::GetSemanticNode: No semantic tree found for koid: " << koid;
    return nullptr;
  }

  return tree_weak_ptr->GetNode(node_id);
}

const fuchsia::accessibility::semantics::Node* ViewManager::GetParentNode(zx_koid_t koid,
                                                                          uint32_t node_id) const {
  auto tree_weak_ptr = GetTreeByKoid(koid);

  if (!tree_weak_ptr) {
    FX_LOGS(ERROR) << "ViewManager::GetSemanticNode: No semantic tree found for koid: " << koid;
    return nullptr;
  }

  return tree_weak_ptr->GetParentNode(node_id);
}

const fuchsia::accessibility::semantics::Node* ViewManager::GetNextNode(
    zx_koid_t koid, uint32_t node_id,
    fit::function<bool(const fuchsia::accessibility::semantics::Node*)> filter) const {
  auto tree_weak_ptr = GetTreeByKoid(koid);

  if (!tree_weak_ptr) {
    FX_LOGS(ERROR) << "ViewManager::GetSemanticNode: No semantic tree found for koid: " << koid;
    return nullptr;
  }

  return tree_weak_ptr->GetNextNode(node_id, std::move(filter));
}

const fuchsia::accessibility::semantics::Node* ViewManager::GetPreviousNode(
    zx_koid_t koid, uint32_t node_id,
    fit::function<bool(const fuchsia::accessibility::semantics::Node*)> filter) const {
  auto tree_weak_ptr = GetTreeByKoid(koid);

  if (!tree_weak_ptr) {
    FX_LOGS(ERROR) << "ViewManager::GetSemanticNode: No semantic tree found for koid: " << koid;
    return nullptr;
  }

  return tree_weak_ptr->GetPreviousNode(node_id, std::move(filter));
}

void ViewManager::ClearHighlight() {
  if (!annotations_enabled_) {
    return;
  }

  if (RemoveHighlight()) {
    highlighted_node_ = std::nullopt;
  }
}

bool ViewManager::RemoveHighlight() {
  if (!highlighted_node_.has_value()) {
    return false;
  }

  auto it = view_wrapper_map_.find(highlighted_node_->koid);
  if (it == view_wrapper_map_.end()) {
    FX_LOGS(ERROR) << "ViewManager::UpdateHighlights: Invalid previously highlighted view koid: "
                   << highlighted_node_->koid;
    return false;
  }

  FX_DCHECK(it->second);
  it->second->ClearHighlights();

  return true;
}

void ViewManager::UpdateHighlight(SemanticNodeIdentifier newly_highlighted_node) {
  if (!annotations_enabled_) {
    return;
  }

  ClearHighlight();

  if (DrawHighlight(newly_highlighted_node)) {
    highlighted_node_ = std::make_optional<SemanticNodeIdentifier>(newly_highlighted_node);
  }
}

bool ViewManager::DrawHighlight(SemanticNodeIdentifier newly_highlighted_node) {
  auto it = view_wrapper_map_.find(newly_highlighted_node.koid);
  if (it == view_wrapper_map_.end()) {
    FX_LOGS(WARNING) << "ViewManager::UpdateHighlights: Invalid newly highlighted view koid: "
                     << newly_highlighted_node.koid;
    return false;
  }

  FX_DCHECK(it->second);
  it->second->HighlightNode(newly_highlighted_node.node_id);

  return true;
}

void ViewManager::ExecuteHitTesting(
    zx_koid_t koid, fuchsia::math::PointF local_point,
    fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback) {
  auto tree_weak_ptr = GetTreeByKoid(koid);

  if (!tree_weak_ptr) {
    FX_LOGS(ERROR) << "ViewManager::ExecuteHitTesting: No semantic tree found for koid: " << koid;
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
    FX_LOGS(ERROR) << "ViewManager::PerformAccessibilityAction: No semantic tree found for koid: "
                   << koid;
    callback(false);
    return;
  }

  tree_weak_ptr->PerformAccessibilityAction(node_id, action, std::move(callback));
}

}  // namespace a11y
