// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/nodes/opacity_node.h"

#include <utility>

namespace scenic {
namespace gfx {

const ResourceTypeInfo OpacityNode::kTypeInfo = {
    ResourceType::kNode | ResourceType::kOpacityNode, "OpacityNode"};

OpacityNode::OpacityNode(Session* session, scenic::ResourceId node_id)
    : Node(session, node_id, OpacityNode::kTypeInfo) {
  FXL_LOG(WARNING) << "Opacity support is limited. Z sorting and shadows may "
                      "not behave correctly.";
}

void OpacityNode::SetOpacity(float opacity) {
  FXL_DCHECK(0 <= opacity && opacity <= 1);
  opacity_ = opacity;
}

}  // namespace gfx
}  // namespace scenic
