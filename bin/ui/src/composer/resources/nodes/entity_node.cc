// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/composer/resources/nodes/entity_node.h"

namespace mozart {
namespace composer {

const ResourceTypeInfo EntityNode::kTypeInfo = {
    ResourceType::kNode | ResourceType::kEntityNode, "EntityNode"};

EntityNode::EntityNode(Session* session)
    : Node(session, EntityNode::kTypeInfo) {}

}  // namespace composer
}  // namespace mozart
