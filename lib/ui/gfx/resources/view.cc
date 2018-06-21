// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/view.h"

#include "garnet/lib/ui/gfx/engine/engine.h"
#include "garnet/lib/ui/gfx/engine/object_linker.h"
#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/resources/view_holder.h"

namespace scenic {
namespace gfx {

const ResourceTypeInfo View::kTypeInfo = {ResourceType::kView, "View"};

View::View(Session* session, scenic::ResourceId id,
           ::fuchsia::ui::gfx::ViewArgs args)
    : Resource(session, id, View::kTypeInfo) {
  ViewLinker* view_linker = session->engine()->view_linker();

  import_handle_ = view_linker->RegisterImport(this, std::move(args.token),
                                                session->error_reporter());
}

View::~View() {
  for (const NodePtr& child : children_) {
    child->Detach();
  }

  if (import_handle_ != 0) {
    ViewLinker* view_linker = session()->engine()->view_linker();

    view_linker->UnregisterImport(import_handle_);
  }
}

void View::LinkResolved(ViewHolder* owner) {
  FXL_DCHECK(!owner_);
  owner_ = owner;

  auto& parent = owner_ ? owner_->parent() : NodePtr();
  if (parent) {
    for (const NodePtr& child : children_) {
      parent->AddChild(child);  // Also detaches child from the old parent.
    }
  } else {
    for (const NodePtr& child : children_) {
      child->Detach();
    }
  }
}

void View::PeerDestroyed() {
  for (const NodePtr& child : children_) {
    child->Detach();
  }
  owner_ = nullptr;
}

void View::ConnectionClosed() {
  for (const NodePtr& child : children_) {
    child->Detach();
  }
  owner_ = nullptr;
}

void View::AddChild(NodePtr child_node) {
  auto& current_parent = owner_ ? owner_->parent() : nullptr;

  // Bail if this Node is already a child of ours.
  if (children_.find(child_node) != children_.end()) {
    FXL_DCHECK(current_parent.get() == child_node->parent());
    return;
  }

  if (current_parent.get() != nullptr) {
    current_parent->AddChild(child_node);
  } else {
    child_node->Detach();
  }
  child_node->set_view(this);
  children_.insert(child_node);
}

void View::DetachChild(Node* child_node) {
  bool found = false;
  auto end = children_.end();
  for (auto it = children_.begin(); it != end; it++) {
    if (it->get() == child_node) {
      children_.erase(it);
      found = true;
      break;
    }
  }
  FXL_DCHECK(found);
}

void View::DetachChildren() {
  for (const NodePtr& child : children_) {
    child->Detach();
  }
  children_.clear();
}

}  // namespace gfx
}  // namespace scenic
