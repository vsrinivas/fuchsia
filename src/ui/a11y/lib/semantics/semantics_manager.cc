// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/semantics_manager.h"

#include <lib/syslog/cpp/logger.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "src/lib/fxl/logging.h"

namespace a11y {

SemanticsManager::SemanticsManager(vfs::PseudoDir* debug_dir) : debug_dir_(debug_dir) {}

SemanticsManager::~SemanticsManager() = default;

void SemanticsManager::RegisterViewForSemantics(
    fuchsia::ui::views::ViewRef view_ref,
    fidl::InterfaceHandle<fuchsia::accessibility::semantics::SemanticListener> handle,
    fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree> semantic_tree_request) {
  // Clients should register every view that gets created irrespective of the
  // state(enabled/disabled) of screen reader.
  // TODO(36199): Check if ViewRef is Valid.
  // TODO(36199): When ViewRef is no longer valid, then all the holders of ViewRef will get a
  // signal, and Semantics Manager should then delete the binding for that ViewRef.

  fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener = handle.Bind();
  semantic_listener.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Semantic Provider disconnected with status: "
                   << zx_status_get_string(status);
  });

  CompleteSemanticRegistration(std::move(view_ref), std::move(semantic_listener),
                               std::move(semantic_tree_request));
}

void SemanticsManager::CompleteSemanticRegistration(
    fuchsia::ui::views::ViewRef view_ref,
    fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener,
    fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree> semantic_tree_request) {
  auto semantic_tree = std::make_unique<SemanticTreeService>(
      std::make_unique<SemanticTree>(), std::move(view_ref), std::move(semantic_listener),
      debug_dir_,
      /*commit_error_callback=*/[this](zx_koid_t koid) { CloseChannel(koid); });
  // As part of the registration, client should get notified about the current Semantics Manager
  // enable settings.
  semantic_tree->EnableSemanticsUpdates(semantics_enabled_);

  // Create Binding.
  semantic_tree_bindings_.AddBinding(std::move(semantic_tree), std::move(semantic_tree_request));
}

fuchsia::accessibility::semantics::NodePtr SemanticsManager::GetAccessibilityNode(
    const fuchsia::ui::views::ViewRef& view_ref, const int32_t node_id) {
  for (auto& binding : semantic_tree_bindings_.bindings()) {
    if (binding->impl()->view_ref_koid() == GetKoid(view_ref)) {
      const auto* node = binding->impl()->Get()->GetNode(node_id);
      if (!node) {
        return nullptr;
      }
      auto node_ptr = fuchsia::accessibility::semantics::Node::New();
      node->Clone(node_ptr.get());
      return node_ptr;
    }
  }

  return nullptr;
}

fuchsia::accessibility::semantics::NodePtr SemanticsManager::GetAccessibilityNodeByKoid(
    const zx_koid_t koid, const int32_t node_id) {
  for (auto& binding : semantic_tree_bindings_.bindings()) {
    if (binding->impl()->view_ref_koid() == koid) {
      const auto* node = binding->impl()->Get()->GetNode(node_id);
      if (!node) {
        return nullptr;
      }
      auto node_ptr = fuchsia::accessibility::semantics::Node::New();
      node->Clone(node_ptr.get());
      return node_ptr;
    }
  }
  return nullptr;
}

void SemanticsManager::SetSemanticsManagerEnabled(bool enabled) {
  semantics_enabled_ = enabled;
  EnableSemanticsUpdates(semantics_enabled_);
}

void SemanticsManager::PerformHitTesting(
    zx_koid_t koid, ::fuchsia::math::PointF local_point,
    fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback) {
  for (auto& binding : semantic_tree_bindings_.bindings()) {
    if (binding->impl()->view_ref_koid() == koid) {
      return binding->impl()->Get()->PerformHitTesting(local_point, std::move(callback));
    }
  }

  FX_LOGS(INFO) << "Given KOID(" << koid << ") doesn't match any existing ViewRef's koid.";
}

void SemanticsManager::CloseChannel(zx_koid_t koid) {
  for (auto& binding : semantic_tree_bindings_.bindings()) {
    if (binding->impl()->view_ref_koid() == koid) {
      semantic_tree_bindings_.RemoveBinding(binding->impl());
    }
  }
}

void SemanticsManager::EnableSemanticsUpdates(bool enabled) {
  // Notify all the Views about change in Semantics Enabled.
  for (auto& binding : semantic_tree_bindings_.bindings()) {
    binding->impl()->EnableSemanticsUpdates(enabled);
  }
}

}  // namespace a11y
