/// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_NODES_ENTITY_NODE_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_NODES_ENTITY_NODE_H_

#include "src/ui/scenic/lib/gfx/resources/nodes/node.h"

namespace scenic_impl {
namespace gfx {

class EntityNode final : public Node {
 public:
  static const ResourceTypeInfo kTypeInfo;

  EntityNode(Session* session, SessionId session_id, ResourceId node_id);

  void Accept(class ResourceVisitor* visitor) override;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_NODES_ENTITY_NODE_H_
