/// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_NODES_OPACITY_NODE_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_NODES_OPACITY_NODE_H_

#include "src/ui/scenic/lib/gfx/resources/nodes/node.h"

namespace scenic_impl {
namespace gfx {

class OpacityNode final : public Node {
 public:
  static const ResourceTypeInfo kTypeInfo;

  OpacityNode(Session* session, SessionId session_id, ResourceId node_id);

  // valid range: [0, 1]
  void SetOpacity(float opacity);

  // valid range: [0, 1]
  float opacity() const { return opacity_; }

  void Accept(class ResourceVisitor* visitor) override;

 private:
  float opacity_ = 1;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_NODES_OPACITY_NODE_H_
