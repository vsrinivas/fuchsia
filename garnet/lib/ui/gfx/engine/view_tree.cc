// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/view_tree.h"

#include <zircon/syscalls/object.h>

#include <utility>

#include "src/lib/fxl/logging.h"

namespace scenic_impl::gfx {

namespace {
// Convenience functions.
bool IsValid(zx_koid_t koid) { return koid != ZX_KOID_INVALID; }

zx_koid_t ExtractKoid(const fuchsia::ui::views::ViewRef& view_ref) {
  zx_info_handle_basic_t info{};
  if (view_ref.reference.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr) !=
      ZX_OK) {
    return ZX_KOID_INVALID;  // no info
  }

  return info.koid;
}

std::optional<zx_koid_t> wrap(zx_koid_t koid) {
  return koid == ZX_KOID_INVALID ? std::nullopt : std::optional(koid);
}
}  // namespace

fuchsia::ui::focus::FocusChain ViewTree::CloneFocusChain() const {
  FXL_DCHECK(IsStateValid()) << "invariant";

  fuchsia::ui::focus::FocusChain full_copy{};
  for (zx_koid_t koid : focus_chain_) {
    full_copy.mutable_focus_chain()->push_back(CloneViewRefOf(koid));
  }
  return full_copy;
}

const std::vector<zx_koid_t>& ViewTree::focus_chain() const { return focus_chain_; }

std::optional<zx_koid_t> ViewTree::ParentOf(zx_koid_t child) const {
  FXL_DCHECK(IsTracked(child)) << "invariant";

  if (const auto ptr = std::get_if<AttachNode>(&nodes_.at(child))) {
    return wrap(ptr->parent);
  } else if (const auto ptr = std::get_if<RefNode>(&nodes_.at(child))) {
    return wrap(ptr->parent);
  }

  FXL_NOTREACHED() << "impossible";
  return std::nullopt;
}

bool ViewTree::IsTracked(zx_koid_t koid) const { return IsValid(koid) && nodes_.count(koid) > 0; }

bool ViewTree::IsConnected(zx_koid_t koid) const {
  FXL_DCHECK(IsTracked(koid)) << "precondition";

  if (!IsValid(root_))
    return false;  // No connectivity, base case.

  if (koid == root_)
    return true;  // Trivial connectivity, base case.

  if (const auto ptr = std::get_if<AttachNode>(&nodes_.at(koid))) {
    if (!IsTracked(ptr->parent))
      return false;  // Does not reach root.
    if (const auto parent_ptr = std::get_if<RefNode>(&nodes_.at(ptr->parent)))
      return IsConnected(ptr->parent);  // Recursive.
  } else if (const auto ptr = std::get_if<RefNode>(&nodes_.at(koid))) {
    if (!IsTracked(ptr->parent))
      return false;  // Does not reach root.
    if (const auto parent_ptr = std::get_if<AttachNode>(&nodes_.at(ptr->parent)))
      return IsConnected(ptr->parent);  // Recursive.
  }
  FXL_NOTREACHED() << "invariant: child/parent types are known and correctly alternate";
  return false;  // Impossible.
}

bool ViewTree::IsRefNode(zx_koid_t koid) const {
  FXL_DCHECK(IsTracked(koid)) << "precondition";
  return std::holds_alternative<RefNode>(nodes_.at(koid));
}

bool ViewTree::IsStateValid() const {
  // Map state
  for (const auto& item : nodes_) {
    if (!IsValid(item.first)) {
      FXL_LOG(ERROR) << "Map key is invalid koid.";
      return false;
    }
    if (const auto ptr = std::get_if<AttachNode>(&item.second)) {
      if (IsValid(ptr->parent)) {
        if (!IsTracked(ptr->parent)) {
          FXL_LOG(ERROR) << "Map item's parent is valid but isn't tracked: " << ptr->parent;
          return false;
        }
        if (!std::holds_alternative<RefNode>(nodes_.at(ptr->parent))) {
          FXL_LOG(ERROR) << "Map item's parent should be a RefNode: " << ptr->parent;
          return false;
        }
      }
    } else if (const auto ptr = std::get_if<RefNode>(&item.second)) {
      if (IsValid(ptr->parent)) {
        if (!IsTracked(ptr->parent)) {
          FXL_LOG(ERROR) << "Map item's parent is valid but isn't tracked: " << ptr->parent;
          return false;
        }
        if (!std::holds_alternative<AttachNode>(nodes_.at(ptr->parent))) {
          FXL_LOG(ERROR) << "Map item's parent should be an AttachNode: " << ptr->parent;
          return false;
        }
        // Only one entity must have ptr->parent as a parent.
        size_t parent_count = 0;
        for (const auto& j_item : nodes_) {
          if (const auto j_ptr = std::get_if<AttachNode>(&j_item.second)) {
            if (j_ptr->parent == ptr->parent)
              ++parent_count;
          } else if (const auto j_ptr = std::get_if<RefNode>(&j_item.second)) {
            if (j_ptr->parent == ptr->parent)
              ++parent_count;
          } else {
            FXL_NOTREACHED() << "unknown type";
            return false;
          }
        }
        if (parent_count != 1) {
          FXL_LOG(ERROR) << "Map item's parent should have just one child: " << ptr->parent
                         << ", count: " << parent_count;
          return false;
        }
      }
    } else {
      FXL_NOTREACHED() << "unknown type";
      return false;
    }
  }

  // Scene state
  if (IsValid(root_)) {
    if (!IsTracked(root_)) {
      FXL_LOG(ERROR) << "Scene is valid but isn't tracked: " << root_;
      return false;
    }
    if (!std::holds_alternative<RefNode>(nodes_.at(root_))) {
      FXL_LOG(ERROR) << "Scene should be a RefNode but isn't: " << root_;
      return false;
    }
  }

  // Focus chain state: relationship with root_.
  if (IsValid(root_)) {
    if (focus_chain_.size() == 0) {
      FXL_LOG(ERROR) << "Focus chain should be not empty but is.";
      return false;
    }
    if (focus_chain_[0] != root_) {
      FXL_LOG(ERROR) << "Focus chain's zeroth element should be root but isn't: " << root_ << ", "
        << focus_chain_[0];
      return false;
    }
  } else {
    if (focus_chain_.size() > 0) {
      FXL_LOG(ERROR) << "Focus chain should be empty but isn't.";
      return false;
    }
  }

  // Focus chain state: relationship with nodes_.
  for (size_t idx = 1; idx < focus_chain_.size(); ++idx) {
    const zx_koid_t koid = focus_chain_[idx];
    if (!IsValid(koid) || !IsTracked(koid) || !IsRefNode(koid)) {
      FXL_LOG(ERROR) << "Focus chain element isn't a valid and tracked RefNode: " << koid
                     << ", at index: " << idx;
      return false;
    }
    const zx_koid_t parent = std::get_if<RefNode>(&nodes_.at(koid))->parent;
    if (!IsValid(parent) || !IsTracked(parent) || IsRefNode(parent)) {
      FXL_LOG(ERROR) << "Focus chain element's parent isn't a valid and tracked AttachNode: "
                     << koid << ", at index: " << idx;
      return false;
    }
    const zx_koid_t grandparent = std::get_if<AttachNode>(&nodes_.at(parent))->parent;
    if (!IsValid(grandparent) || !IsTracked(grandparent) || !IsRefNode(grandparent)) {
      FXL_LOG(ERROR) << "Focus chain element's grandparent isn't a valid and tracked RefNode: "
                     << koid << ", at index: " << idx;
      return false;
    }
    if (grandparent != focus_chain_[idx - 1]) {
      FXL_LOG(ERROR)
          << "Focus chain element's grandparent doesn't match previous focus chain element: "
          << koid << ", at index: " << idx;
      return false;
    }
  }
  return true;
}

bool ViewTree::RequestFocusChange(const zx_koid_t requestor, const zx_koid_t request) {
  // Invalid requestor.
  if (!IsTracked(requestor) || !IsRefNode(requestor) || !IsConnected(requestor))
    return false;

  // Invalid request.
  if (!IsTracked(request) || !IsRefNode(request) || !IsConnected(request))
    return false;

  // Transfer policy: requestor must be authorized.
  {
    bool has_authority = false;
    for (zx_koid_t authority : focus_chain_) {
      if (requestor == authority) {
        has_authority = true;
        break;
      }
    }
    if (!has_authority)
      return false;
  }

  // Transfer policy: requestor must be an ancestor of request.
  {
    bool is_ancestor = false;
    zx_koid_t curr = request;
    while (IsValid(curr)) {
      if (curr == requestor) {
        is_ancestor = true;
        break;
      }

      // Iterate upward.
      if (const auto ptr = std::get_if<RefNode>(&nodes_.at(curr))) {
        curr = ptr->parent;
      } else if (const auto ptr = std::get_if<AttachNode>(&nodes_.at(curr))) {
        curr = ptr->parent;
      } else {
        FXL_NOTREACHED() << "impossible";
        return false;
      }
    }
    if (!is_ancestor)
      return false;
  }

  // It's a valid request for a change to focus chain. Regenerate chain.
  {
    std::vector<zx_koid_t> buffer;
    zx_koid_t curr = request;
    while (IsValid(curr)) {
      if (const auto ptr = std::get_if<RefNode>(&nodes_.at(curr))) {
        buffer.push_back(curr);
        curr = ptr->parent;
      } else if (const auto ptr = std::get_if<AttachNode>(&nodes_.at(curr))) {
        curr = ptr->parent;
      } else {
        FXL_NOTREACHED() << "impossible";
        return false;
      }
    }

    // Clear state; populate with root and move downwards.
    focus_chain_.clear();
    for (auto iter = buffer.crbegin(); iter != buffer.crend(); ++iter) {
      focus_chain_.push_back(*iter);
    }
  }

  FXL_DCHECK(IsStateValid()) << "postcondition";
  return true;
}

void ViewTree::NewRefNode(fuchsia::ui::views::ViewRef view_ref) {
  const zx_koid_t koid = ExtractKoid(view_ref);
  FXL_DCHECK(IsValid(koid)) << "precondition";
  FXL_DCHECK(!IsTracked(koid)) << "precondition";

  if (!IsValid(koid) || IsTracked(koid))
    return;  // Bail.

  nodes_[koid] = RefNode{.view_ref = std::move(view_ref)};

  FXL_DCHECK(IsStateValid()) << "postcondition";
}

void ViewTree::NewAttachNode(zx_koid_t attach_point) {
  FXL_DCHECK(IsValid(attach_point)) << "precondition";
  FXL_DCHECK(!IsTracked(attach_point)) << "precondition";

  if (!IsValid(attach_point) || IsTracked(attach_point))
    return;  // Bail.

  nodes_[attach_point] = AttachNode{};

  FXL_DCHECK(IsStateValid()) << "postcondition";
}

void ViewTree::DeleteNode(const zx_koid_t koid) {
  FXL_DCHECK(IsTracked(koid)) << "precondition";

  // Remove |koid| if it is a parent.
  for (auto& item : nodes_) {
    if (auto ptr = std::get_if<AttachNode>(&item.second)) {
      if (ptr->parent == koid) {
        ptr->parent = ZX_KOID_INVALID;
      }
    } else if (auto ptr = std::get_if<RefNode>(&item.second)) {
      if (ptr->parent == koid) {
        ptr->parent = ZX_KOID_INVALID;
      }
    } else {
      FXL_NOTREACHED() << "unknown type";
    }
  }
  nodes_.erase(koid);

  // Remove |koid| if it is the root.
  if (root_ == koid) {
    root_ = ZX_KOID_INVALID;
  }

  // Remove |koid| if it is in the focus chain.
  RepairFocus();

  FXL_DCHECK(IsStateValid()) << "postcondition";
}

void ViewTree::MakeRoot(zx_koid_t koid) {
  FXL_DCHECK(!IsValid(koid) || (IsTracked(koid) && IsRefNode(koid))) << "precondition";

  root_ = koid;

  RepairFocus();

  FXL_DCHECK(IsStateValid()) << "postcondition";
}

void ViewTree::ConnectToParent(zx_koid_t child, zx_koid_t parent) {
  FXL_DCHECK(IsTracked(child)) << "precondition";
  FXL_DCHECK(IsTracked(parent)) << "precondition";

  if (auto ptr = std::get_if<AttachNode>(&nodes_[child])) {
    if (std::holds_alternative<RefNode>(nodes_[parent])) {
      ptr->parent = parent;
      FXL_DCHECK(IsStateValid()) << "postcondition";
      return;
    }
  } else if (auto ptr = std::get_if<RefNode>(&nodes_[child])) {
    if (std::holds_alternative<AttachNode>(nodes_[parent])) {
      ptr->parent = parent;
      FXL_DCHECK(IsStateValid()) << "postcondition";
      return;
    }
  }
  FXL_NOTREACHED() << "invariant: child/parent types must be known and must be different";
}

void ViewTree::DisconnectFromParent(zx_koid_t child) {
  FXL_DCHECK(IsTracked(child)) << "precondition";

  if (auto ptr = std::get_if<AttachNode>(&nodes_[child])) {
    FXL_DCHECK(IsTracked(ptr->parent)) << "precondition";
    if (!IsValid(ptr->parent) || !IsTracked(ptr->parent))
      return;  // Bail.

    if (auto parent_ptr = std::get_if<RefNode>(&nodes_[ptr->parent])) {
      ptr->parent = ZX_KOID_INVALID;
      RepairFocus();
      FXL_DCHECK(IsStateValid()) << "postcondition";
      return;
    }
  } else if (auto ptr = std::get_if<RefNode>(&nodes_[child])) {
    FXL_DCHECK(IsTracked(ptr->parent)) << "precondition";
    if (!IsTracked(ptr->parent))
      return;  // Bail.

    if (auto parent_ptr = std::get_if<AttachNode>(&nodes_[ptr->parent])) {
      ptr->parent = ZX_KOID_INVALID;
      RepairFocus();
      FXL_DCHECK(IsStateValid()) << "postcondition";
      return;
    }
  }
  FXL_NOTREACHED() << "invariant: child/parent types are known and correct";
}

fuchsia::ui::views::ViewRef ViewTree::CloneViewRefOf(zx_koid_t koid) const {
  FXL_DCHECK(IsTracked(koid)) << "precondition";
  FXL_DCHECK(IsRefNode(koid)) << "precondition";
  fuchsia::ui::views::ViewRef clone;
  if (const auto ptr = std::get_if<RefNode>(&nodes_.at(koid))) {
    fidl::Clone(ptr->view_ref, &clone);
  }
  return clone;
}

void ViewTree::RepairFocus() {
  // root_ was destroyed, set focus_chain_ to empty.
  if (!IsTracked(root_)) {
    FXL_DCHECK(!IsValid(root_)) << "invariant";
    focus_chain_.clear();
    return;
  }

  // root_ exists, but it's fresh or a replacement. Use it.
  if (focus_chain_.size() == 0 || focus_chain_[0] != root_) {
    focus_chain_.clear();
    focus_chain_.push_back(root_);
    return;
  }

  // root_ exists, and is already valid.
  // Walk down chain until we find a divergence from relationship data in nodes_.
  size_t curr = 1;
  for (; curr < focus_chain_.size(); ++curr) {
    const zx_koid_t child = focus_chain_[curr];
    if (!IsTracked(child))
      break;  // child destroyed
    const std::optional<zx_koid_t> parent = ParentOf(child);
    if (!parent.has_value())
      break;  // parent reset or destroyed
    const std::optional<zx_koid_t> grandparent = ParentOf(parent.value());
    if (!grandparent.has_value())
      break;  // grandparent reset or destroyed
    if (grandparent.value() != focus_chain_[curr - 1])
      break;  // focus chain relation changed
  }

  // Trim out invalid entries.
  FXL_DCHECK(curr >= 1 && curr <= focus_chain_.size()) << "invariant";
  focus_chain_.erase(focus_chain_.begin() + curr, focus_chain_.end());

  // Run state validity check at call site.
}

}  // namespace scenic_impl::gfx
