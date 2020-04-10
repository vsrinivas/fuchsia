// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/view/view_manager.h"

#include <lib/async/default.h>
#include <lib/fit/bridge.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "src/lib/fxl/logging.h"
#include "src/ui/a11y/lib/util/util.h"

namespace a11y {

std::unique_ptr<SemanticTreeService> SemanticTreeServiceFactory::NewService(
    zx_koid_t koid, fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener,
    vfs::PseudoDir* debug_dir, SemanticTreeService::CloseChannelCallback close_channel_callback) {
  auto semantic_tree = std::make_unique<SemanticTreeService>(
      std::make_unique<SemanticTree>(), koid, std::move(semantic_listener), debug_dir,
      std::move(close_channel_callback));
  return semantic_tree;
}

ViewManager::ViewManager(std::unique_ptr<SemanticTreeServiceFactory> factory,
                         std::unique_ptr<ViewWrapperFactory> view_wrapper_factory,
                         vfs::PseudoDir* debug_dir)
    : factory_(std::move(factory)),
      view_wrapper_factory_(std::move(view_wrapper_factory)),
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
  // TODO(36199): Check if ViewRef is Valid.
  // TODO(36199): When ViewRef is no longer valid, then all the holders of ViewRef will get a
  // signal, and Semantics Manager should then delete the binding for that ViewRef.

  zx_koid_t koid = GetKoid(view_ref);

  auto close_channel_callback = [this, koid]() {
    wait_map_.erase(koid);
    view_wrapper_map_.erase(koid);
  };

  fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener = handle.Bind();
  semantic_listener.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Semantic Provider disconnected with status: "
                   << zx_status_get_string(status);
  });

  auto service = factory_->NewService(koid, std::move(semantic_listener), debug_dir_,
                                      std::move(close_channel_callback));

  // As part of the registration, client should get notified about the current Semantics Manager
  // enable settings.
  service->EnableSemanticsUpdates(semantics_enabled_);

  // Start listening for signals on the view ref so that we can clean up associated state.
  auto wait_ptr = std::make_unique<async::WaitMethod<ViewManager, &ViewManager::ViewSignalHandler>>(
      this, view_ref.reference.get(), ZX_EVENTPAIR_PEER_CLOSED);
  FX_CHECK(wait_ptr->Begin(async_get_default_dispatcher()) == ZX_OK);
  wait_map_[koid] = std::move(wait_ptr);
  view_wrapper_map_[koid] = view_wrapper_factory_->CreateViewWrapper(
      std::move(view_ref), std::move(service), std::move(semantic_tree_request));
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

const fuchsia::accessibility::semantics::Node* ViewManager::GetNextNode(zx_koid_t koid,
                                                                        uint32_t node_id) const {
  auto tree_weak_ptr = GetTreeByKoid(koid);

  if (!tree_weak_ptr) {
    FX_LOGS(ERROR) << "ViewManager::GetSemanticNode: No semantic tree found for koid: " << koid;
    return nullptr;
  }

  return tree_weak_ptr->GetNextNode(node_id);
}

const fuchsia::accessibility::semantics::Node* ViewManager::GetPreviousNode(
    zx_koid_t koid, uint32_t node_id) const {
  auto tree_weak_ptr = GetTreeByKoid(koid);

  if (!tree_weak_ptr) {
    FX_LOGS(ERROR) << "ViewManager::GetSemanticNode: No semantic tree found for koid: " << koid;
    return nullptr;
  }

  return tree_weak_ptr->GetPreviousNode(node_id);
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
    return;
  }

  tree_weak_ptr->PerformAccessibilityAction(node_id, action, std::move(callback));
}

}  // namespace a11y
