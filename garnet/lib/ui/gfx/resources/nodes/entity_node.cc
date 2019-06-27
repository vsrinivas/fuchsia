// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/nodes/entity_node.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo EntityNode::kTypeInfo = {ResourceType::kNode | ResourceType::kEntityNode,
                                                "EntityNode"};

EntityNode::EntityNode(Session* session, ResourceId node_id)
    : Node(session, node_id, EntityNode::kTypeInfo) {}

}  // namespace gfx
}  // namespace scenic_impl
