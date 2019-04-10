// Copyright 2019. The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/nodes/view_node.h"

#include "garnet/lib/ui/gfx/engine/session.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo ViewNode::kTypeInfo = {
    ResourceType::kNode | ResourceType::kView, "ViewNode"};

ViewNode::ViewNode(Session* session, ResourceId view_id)
    : Node(session, /* node_id */ 0, ViewNode::kTypeInfo), view_id_(view_id) {}

ViewPtr ViewNode::FindOwningView() const {
  return session()->resources()->FindResource<View>(view_id_);
}

}  // namespace gfx
}  // namespace scenic_impl
