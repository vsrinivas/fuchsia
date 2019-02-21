// Copyright 2019. The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/nodes/view_node.h"

#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/resources/view.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo ViewNode::kTypeInfo = {
    ResourceType::kNode | ResourceType::kView, "ViewNode"};

ViewNode::ViewNode(Session* session, ResourceId view_id)
    : Node(session, /* node_id */ 0, ViewNode::kTypeInfo), view_id_(view_id) {}

ResourcePtr ViewNode::FindOwningView() const {
  return session()->resources()->FindResource<View>(view_id_);
}

ResourcePtr ViewNode::FindOwningViewOrImportNode() const {
  return this->FindOwningView();
}

View* ViewNode::GetView() const {
  return session()->resources()->FindResource<View>(view_id_).get();
}

}  // namespace gfx
}  // namespace scenic_impl
