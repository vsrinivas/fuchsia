// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/src/scene/resources/nodes/entity_node.h"
#include "apps/mozart/src/scene/resources/nodes/hit_test_result.h"
#include "lib/ftl/macros.h"

namespace mozart {
namespace scene {

class TagNode final : public Node {
 public:
  static const ResourceTypeInfo kTypeInfo;

  TagNode(Session* session, ResourceId node_id, int32_t tag);

  int32_t tag() const { return tag_value_; }

  /// Returns the list of tag nodes whose children have accepted the hit test.
  /// The hit test results are in the coordinate space of the nearest tag
  /// node. The point is in the coordinate space of the node being queried.
  /// A hit test may only be initiated at a tag node.
  HitTestResults HitTest(const escher::vec2& point) const;

 private:
  int32_t tag_value_;

  bool HitTestVisitNode(const Node& child_node,
                        HitTestResults& results,
                        const escher::vec2& point) const;

  FTL_DISALLOW_COPY_AND_ASSIGN(TagNode);
};

}  // namespace scene
}  // namespace mozart
