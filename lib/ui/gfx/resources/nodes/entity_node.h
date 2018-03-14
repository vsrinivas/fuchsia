/// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_NODES_ENTITY_NODE_H_
#define GARNET_LIB_UI_GFX_RESOURCES_NODES_ENTITY_NODE_H_

#include "garnet/lib/ui/gfx/resources/nodes/node.h"

namespace scenic {
namespace gfx {

class EntityNode final : public Node {
 public:
  static const ResourceTypeInfo kTypeInfo;

  EntityNode(Session* session, scenic::ResourceId node_id);

  void Accept(class ResourceVisitor* visitor) override;
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_RESOURCES_NODES_ENTITY_NODE_H_
