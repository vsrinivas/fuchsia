// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/view.h"

#include "garnet/lib/ui/gfx/engine/engine.h"
#include "garnet/lib/ui/gfx/engine/object_linker.h"
#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/resources/nodes/node.h"
#include "garnet/lib/ui/gfx/resources/view_holder.h"
#include "lib/fxl/logging.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo View::kTypeInfo = {ResourceType::kView, "View"};

View::View(Session* session, ResourceId id, ViewLinker::ImportLink link)
    : Resource(session, id, View::kTypeInfo), link_(std::move(link)) {
  FXL_DCHECK(link_.valid());
  FXL_DCHECK(!link_.initialized());
}

View::~View() {
  for (const NodePtr& child : children_) {
    child->set_view(nullptr);
    child->Detach();
  }
}

void View::Connect() {
  link_.Initialize(this, fit::bind_member(this, &View::LinkResolved),
                   fit::bind_member(this, &View::LinkDisconnected));
}

bool View::AddChild(NodePtr child) {
  auto* parent_node = view_holder_ ? view_holder_->parent() : nullptr;

  // Bail if this Node is already a child of ours.
  if (children_.find(child) != children_.end()) {
    FXL_DCHECK(parent_node == child->parent());
    return false;
  }

  // Link the child to our parent, and set its view to us.
  if (parent_node != nullptr) {
    parent_node->AddChild(child);
  } else {
    child->Detach();
  }
  child->set_view(this);
  children_.insert(child);

  return true;
}

void View::DetachChildren() {
  for (const NodePtr& child : children_) {
    child->Detach();
  }
  children_.clear();
}

void View::SignalRender() {
  if (!render_handle_) {
    return;
  }

  // Verify the render_handle_ is still valid before attempting to signal it.
  if (zx_object_get_info(render_handle_, ZX_INFO_HANDLE_VALID, /*buffer=*/NULL,
                         /*buffer_size=*/0, /*actual=*/NULL,
                         /*avail=*/NULL) == ZX_OK) {
    zx_status_t status =
        zx_object_signal(render_handle_, /*clear_mask=*/0u, ZX_EVENT_SIGNALED);
    ZX_ASSERT(status == ZX_OK);
  }
}

void View::LinkResolved(ViewHolder* view_holder) {
  FXL_DCHECK(!view_holder_);
  view_holder_ = view_holder;

  auto* parent_node = view_holder_->parent();
  if (parent_node) {
    for (const NodePtr& child : children_) {
      parent_node->AddChild(child);  // Also detaches child from the old parent.
    }
  } else {
    for (const NodePtr& child : children_) {
      child->Detach();
    }
  }
}

void View::LinkDisconnected() {
  // Make sure the parent and child Nodes' connections to each other remain
  // consistent.
  for (const NodePtr& child : children_) {
    child->Detach();
  }

  view_holder_ = nullptr;
  // ViewHolder was disconnected. There are no guarantees on liveness of the
  // render event, so invalidate the handle.
  InvalidateRenderEventHandle();

  SendViewHolderDisconnectedEvent();
}

void View::RemoveChild(Node* child) {
  // It is OK to use the temporary RefPtr here, as child is guaranteed to
  // already have at least one other reference.  The RefPtr allows for easy
  // lookup into the set.
  size_t erase_count = children_.erase(NodePtr(child));
  FXL_DCHECK(erase_count == 1);
}

void View::SendViewHolderDisconnectedEvent() {
  fuchsia::ui::gfx::Event event;
  event.set_view_holder_disconnected({.view_id = id()});
  session()->EnqueueEvent(std::move(event));
}

}  // namespace gfx
}  // namespace scenic_impl
