// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/bin/a11y_manager/semantics/semantics_manager_impl.h"

#include <lib/syslog/cpp/logger.h>
#include <zircon/status.h>

namespace a11y_manager {

SemanticsManagerImpl::SemanticsManagerImpl() { enabled_ = false; }

void SemanticsManagerImpl::SetDebugDirectory(vfs::PseudoDir* debug_dir) { debug_dir_ = debug_dir; }

void SemanticsManagerImpl::AddBinding(
    fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticsManager> request) {
  bindings_.AddBinding(this, std::move(request));
}

void SemanticsManagerImpl::RegisterView(
    fuchsia::ui::views::ViewRef view_ref,
    fidl::InterfaceHandle<fuchsia::accessibility::semantics::SemanticActionListener> handle,
    fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree> semantic_tree_request) {
  // During View Registration, Semantics manager will ignore enabled flag, to
  // avoid race condition with Semantic Provider(flutter/chrome, etc) since both
  // semantic provider and semantics manager will be notified together about a
  // change in settings.
  // Semantics Manager clears out old bindings when Screen Reader is
  // disabled, and will rely on clients to make sure they only try to register
  // views when screen reader is enabled.

  fuchsia::accessibility::semantics::SemanticActionListenerPtr action_listener = handle.Bind();
  // TODO(MI4-1736): Log View information in below error handler, once ViewRef
  // support is added.
  action_listener.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Semantic Provider disconnected with status: "
                   << zx_status_get_string(status);
  });

  auto semantic_tree_impl = std::make_unique<SemanticTreeImpl>(
      std::move(view_ref), std::move(action_listener), debug_dir_);

  semantic_tree_bindings_.AddBinding(std::move(semantic_tree_impl),
                                     std::move(semantic_tree_request));
}

fuchsia::accessibility::semantics::NodePtr SemanticsManagerImpl::GetAccessibilityNode(
    const fuchsia::ui::views::ViewRef& view_ref, const int32_t node_id) {
  for (auto& binding : semantic_tree_bindings_.bindings()) {
    if (binding->impl()->IsSameView(view_ref))
      return binding->impl()->GetAccessibilityNode(node_id);
  }
  return nullptr;
}

fuchsia::accessibility::semantics::NodePtr SemanticsManagerImpl::GetAccessibilityNodeByKoid(
    const zx_koid_t koid, const int32_t node_id) {
  for (auto& binding : semantic_tree_bindings_.bindings()) {
    if (binding->impl()->IsSameKoid(koid))
      return binding->impl()->GetAccessibilityNode(node_id);
  }
  return nullptr;
}

void SemanticsManagerImpl::SetSemanticsManagerEnabled(bool enabled) {
  if ((enabled_ != enabled) && !enabled) {
    FX_LOGS(INFO) << "Resetting SemanticsTree since SemanticsManager is disabled.";
    bindings_.CloseAll();
    semantic_tree_bindings_.CloseAll();
  }
  enabled_ = enabled;
}

void SemanticsManagerImpl::PerformHitTesting(
    zx_koid_t koid, ::fuchsia::math::PointF local_point,
    fuchsia::accessibility::semantics::SemanticActionListener::HitTestCallback callback) {
  for (auto& binding : semantic_tree_bindings_.bindings()) {
    if (binding->impl()->IsSameKoid(koid)) {
      return binding->impl()->PerformHitTesting(local_point, std::move(callback));
    }
  }

  FX_LOGS(INFO) << "Given KOID(" << koid << ") doesn't match any existing ViewRef's koid.";
}

}  // namespace a11y_manager
