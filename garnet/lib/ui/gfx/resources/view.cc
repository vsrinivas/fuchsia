// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/view.h"

#include "garnet/lib/ui/gfx/engine/engine.h"
#include "garnet/lib/ui/gfx/engine/object_linker.h"
#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/resources/nodes/node.h"
#include "src/lib/fxl/logging.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo View::kTypeInfo = {ResourceType::kView, "View"};

View::View(Session* session, ResourceId id, ViewLinker::ImportLink link)
    :Resource(session, id, View::kTypeInfo), link_(std::move(link)),
    weak_factory_(this) {
  node_ = fxl::AdoptRef<ViewNode>(new ViewNode(session, id));

  FXL_DCHECK(link_.valid());
  FXL_DCHECK(!link_.initialized());
}

View::~View() {
  // Explicitly detach the phantom node to ensure it is cleaned up.
  node_->Detach();
}

void View::Connect() {
  link_.Initialize(this, fit::bind_member(this, &View::LinkResolved),
                   fit::bind_member(this, &View::LinkDisconnected));
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
  view_holder_->AddChild(node_);

  SendViewHolderConnectedEvent();
}

void View::LinkDisconnected() {
  // The connection ViewHolder no longer exists, detach the phantom node from
  // the ViewHolder.
  node_->Detach();

  view_holder_ = nullptr;
  // ViewHolder was disconnected. There are no guarantees on liveness of the
  // render event, so invalidate the handle.
  InvalidateRenderEventHandle();

  SendViewHolderDisconnectedEvent();
}

void View::SendViewHolderConnectedEvent() {
  fuchsia::ui::gfx::Event event;
  event.set_view_holder_connected({.view_id = id()});
  session()->EnqueueEvent(std::move(event));
}

void View::SendViewHolderDisconnectedEvent() {
  fuchsia::ui::gfx::Event event;
  event.set_view_holder_disconnected({.view_id = id()});
  session()->EnqueueEvent(std::move(event));
}

}  // namespace gfx
}  // namespace scenic_impl
