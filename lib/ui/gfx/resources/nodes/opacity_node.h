/// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_NODES_OPACITY_NODE_H_
#define GARNET_LIB_UI_GFX_RESOURCES_NODES_OPACITY_NODE_H_

#include "garnet/lib/ui/gfx/resources/nodes/node.h"

namespace scenic {
namespace gfx {

class OpacityNode final : public Node {
 public:
  static const ResourceTypeInfo kTypeInfo;

  OpacityNode(Session* session, scenic::ResourceId node_id);

  // valid range: [0, 1]
  void SetOpacity(float opacity);

  // valid range: [0, 1]
  float opacity() const { return opacity_; }

  void Accept(class ResourceVisitor* visitor) override;

 private:
  float opacity_ = 1;
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_RESOURCES_NODES_OPACITY_NODE_H_
