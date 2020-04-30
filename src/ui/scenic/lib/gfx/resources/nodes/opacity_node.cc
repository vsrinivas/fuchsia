// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/nodes/opacity_node.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo OpacityNode::kTypeInfo = {ResourceType::kNode | ResourceType::kOpacityNode,
                                                 "OpacityNode"};

OpacityNode::OpacityNode(Session* session, SessionId session_id, ResourceId node_id)
    : Node(session, session_id, node_id, OpacityNode::kTypeInfo) {}

void OpacityNode::SetOpacity(float opacity) {
  FX_DCHECK(0 <= opacity && opacity <= 1);
  opacity_ = opacity;
}

}  // namespace gfx
}  // namespace scenic_impl
