// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/focus/focus_manager.h"

#include <lib/syslog/cpp/macros.h>

namespace focus {

FocusChangeStatus FocusManager::RequestFocus(zx_koid_t requestor, zx_koid_t request) {
  // Invalid requestor.
  if (snapshot_->view_tree.count(requestor) == 0) {
    return FocusChangeStatus::kErrorRequestorInvalid;
  }

  // Invalid request.
  if (snapshot_->view_tree.count(request) == 0) {
    return FocusChangeStatus::kErrorRequestInvalid;
  }

  // Transfer policy: requestor must be authorized.
  if (std::find(focus_chain_.begin(), focus_chain_.end(), requestor) == focus_chain_.end()) {
    return FocusChangeStatus::kErrorRequestorNotAuthorized;
  }

  // Transfer policy: requestor must be ancestor of request
  if (!snapshot_->IsDescendant(/*descendant_koid*/ request, /*ancestor_koid*/ requestor) &&
      request != requestor) {
    return FocusChangeStatus::kErrorRequestorNotRequestAncestor;
  }

  // Transfer policy: request must be focusable
  if (!snapshot_->view_tree.at(request).is_focusable) {
    return FocusChangeStatus::kErrorRequestCannotReceiveFocus;
  }

  // It's a valid request for a change to focus chain.
  SetFocus(request);
  FX_DCHECK(focus_chain_.at(0) == snapshot_->root);
  return FocusChangeStatus::kAccept;
}

void FocusManager::OnNewViewTreeSnapshot(std::shared_ptr<const view_tree::Snapshot> snapshot) {
  FX_DCHECK(snapshot);
  snapshot_ = std::move(snapshot);
  RepairFocus();
}

void FocusManager::RepairFocus() {
  // Old root no longer valid -> move focus to new root.
  if (focus_chain_.empty() || snapshot_->root != focus_chain_.front()) {
    SetFocus(snapshot_->root);
    return;
  }

  // See if there's any place where the old focus chain breaks a parent-child relationship, and
  // truncate from there.
  // Note: Start at i = 1 so we can compare with i - 1.
  for (size_t child_index = 1; child_index < focus_chain_.size(); ++child_index) {
    const zx_koid_t child = focus_chain_.at(child_index);
    const zx_koid_t parent = focus_chain_.at(child_index - 1);
    if (snapshot_->view_tree.count(child) == 0 || snapshot_->view_tree.at(child).parent != parent) {
      focus_chain_.erase(focus_chain_.begin() + child_index, focus_chain_.end());
      break;
    }
  }
}

void FocusManager::SetFocus(zx_koid_t koid) {
  FX_DCHECK(koid != ZX_KOID_INVALID || koid == snapshot_->root);
  if (koid != ZX_KOID_INVALID) {
    FX_DCHECK(snapshot_->view_tree.count(koid) != 0);
    FX_DCHECK(snapshot_->view_tree.at(koid).is_focusable);
  }

  // Regenerate chain.
  focus_chain_.clear();
  while (koid != ZX_KOID_INVALID) {
    focus_chain_.emplace_back(koid);
    koid = snapshot_->view_tree.at(koid).parent;
  }
  std::reverse(focus_chain_.begin(), focus_chain_.end());
}

}  // namespace focus
