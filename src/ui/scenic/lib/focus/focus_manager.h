// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FOCUS_FOCUS_MANAGER_H_
#define SRC_UI_SCENIC_LIB_FOCUS_FOCUS_MANAGER_H_

#include "src/ui/scenic/lib/view_tree/snapshot_types.h"

namespace focus {

// Provide detail on if/why focus change request was denied.
// Specific error-handling policy is responsibility of caller.
enum class FocusChangeStatus {
  kAccept = 0,
  kErrorRequestorInvalid,
  kErrorRequestInvalid,
  kErrorRequestorNotAuthorized,
  kErrorRequestorNotRequestAncestor,
  kErrorRequestCannotReceiveFocus,
  kErrorUnhandledCase,  // last
};

// Class for tracking focus state.
class FocusManager final {
 public:
  // Request focus transfer to the proposed ViewRef's KOID |request|, on the behalf of |requestor|.
  // Return kAccept if successful.
  // - If |requestor| is not authorized to focus |request|, return error.
  // - If the |request| is not in |snapshot_.view_tree|, return error.
  // - If the |request| is otherwise valid, but violates the focus transfer policy, return error.
  FocusChangeStatus RequestFocus(zx_koid_t requestor, zx_koid_t request);

  // Saves the new snapshot and updates the focus chain accordingly.
  void OnNewViewTreeSnapshot(std::shared_ptr<const view_tree::Snapshot> snapshot);

  const std::vector<zx_koid_t>& focus_chain() { return focus_chain_; }

 private:
  // Ensure the focus chain is valid; preserve as much of the existing focus chain as possible.
  // - If the focus chain is still valid, do nothing.
  // - Otherwise, truncate the focus chain so that every pairwise parent-child relationship is valid
  //   in the current tree.
  // - If the entire focus chain is invalid, the new focus chain will contain only the new root.
  // - If the view tree is empty, the new focus chain is empty.
  void RepairFocus();

  // Transfers focus to |koid| and generates the new focus chain.
  //  - |koid| must be either be allowed to receive focus and must exist in the current view tree
  // snapshot.
  //  - If the |snapshot_| is empty, then |koid| is allowed to be ZX_KOID_INVALID and will generate
  // an empty focus_chain_.
  void SetFocus(zx_koid_t koid);

  std::vector<zx_koid_t> focus_chain_;

  std::shared_ptr<const view_tree::Snapshot> snapshot_ =
      std::make_shared<const view_tree::Snapshot>();
};

}  // namespace focus

#endif  // SRC_UI_SCENIC_LIB_FOCUS_FOCUS_MANAGER_H_
