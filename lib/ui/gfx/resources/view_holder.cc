// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/view_holder.h"

#include "garnet/lib/ui/gfx/engine/engine.h"
#include "garnet/lib/ui/gfx/engine/object_linker.h"
#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/resources/view.h"

namespace scenic {
namespace gfx {

const ResourceTypeInfo ViewHolder::kTypeInfo = {ResourceType::kViewHolder,
                                                "ViewHolder"};

ViewHolder::ViewHolder(Session* session, scenic::ResourceId id,
                       ::fuchsia::ui::gfx::ViewHolderArgs args)
    : Resource(session, id, ViewHolder::kTypeInfo) {
  ViewLinker* view_linker = session->engine()->view_linker();

  export_handle_ = view_linker->RegisterExport(this, std::move(args.token),
                                               session->error_reporter());
}

ViewHolder::~ViewHolder() {
  // The child (if any) cleans things up in its PeerDestroyed handler,
  // including Detaching any grandchild Nodes from the parent.
  if (export_handle_ != 0) {
    ViewLinker* view_linker = session()->engine()->view_linker();

    view_linker->UnregisterExport(export_handle_);
  }
}

void ViewHolder::LinkResolved(ViewLinker* linker, View* child) {
  // The child will also receive a LinkResolved call, and it will take care of
  // linking up the Nodes.
  FXL_DCHECK(!child_ && child);
  child_ = child;

  // This guarantees that the View is notified of any previously-set
  // ViewProperties.  Otherwise, e.g. if the ViewHolder properties were set
  // only once before the link was resolved, the View would never be notified.
  SendViewPropertiesChangedEvent();
}

void ViewHolder::PeerDestroyed() {
  // The child is already dead and it cleans things up in its destructor,
  // including Detaching any grandchild Nodes from the parent.
  child_ = nullptr;
}

void ViewHolder::ConnectionClosed() {
  // In this case, there was never any child and there never will be, so there
  // is no need to bother cleaning anything else up.
  child_ = nullptr;
}

void ViewHolder::SetParent(NodePtr parent) {
  parent_ = parent;

  if (child_ != nullptr) {
    if (parent_) {
      for (const NodePtr& grandchild : child_->children()) {
        parent_->AddChild(grandchild);  // Also detaches from the old parent.
      }
    } else {
      for (const NodePtr& grandchild : child_->children()) {
        grandchild->Detach();
      }
    }
  }
}

void ViewHolder::SetViewProperties(fuchsia::ui::gfx::ViewProperties props) {
  if (props != view_properties_) {
    view_properties_ = std::move(props);
    if (child_ != nullptr) {
      SendViewPropertiesChangedEvent();
    }
  }
}

void ViewHolder::SendViewPropertiesChangedEvent() {
  FXL_DCHECK(child_);

  fuchsia::ui::gfx::Event event;
  event.set_view_properties_changed(
      {.view_id = child_->id(), .properties = view_properties_});
  child_->session()->EnqueueEvent(std::move(event));
}

}  // namespace gfx
}  // namespace scenic
