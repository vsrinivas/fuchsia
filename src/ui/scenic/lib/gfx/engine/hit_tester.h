// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_HIT_TESTER_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_HIT_TESTER_H_

#include "src/ui/scenic/lib/gfx/engine/hit.h"
#include "src/ui/scenic/lib/gfx/engine/hit_accumulator.h"

namespace scenic_impl {
namespace gfx {

class Node;
class Session;

// Performs a hit test on the contents of a node.
class HitTester {
 public:
  HitTester() = default;
  virtual ~HitTester() = default;

  // Performs a hit test along the specified ray, adding hit candidates to the given accumulator.
  // The accumulator determines which hits are kept and how they are handled. The ray and the node
  // should be in the same coordinate system (before applying the node's own transform).
  void HitTest(Node* node, const escher::ray4& ray, HitAccumulator<NodeHit>* accumulator);

 private:
  // Accumulates hit test results from the node, as seen by its parent.
  // Applies the node's transform to the ray stack.
  // |ray_info_| must be in the parent's local coordinate system.
  void AccumulateHitsOuter(Node* node);

  // Accumulates hit test results from the node's content and children.
  // |ray_info_| must be in the node's local coordinate system.
  void AccumulateHitsInner(Node* node);

  CollisionAccumulator collision_reporter_;
  HitAccumulator<NodeHit>* accumulator_ = nullptr;

  // The current ray.
  // Null if there is no hit test currently in progress.
  // TODO(SCN-909): Refactor out.
  const escher::ray4* ray_ = nullptr;

  // The current intersection information.
  // NULL if we haven't intersected anything yet.
  Node::IntersectionInfo* intersection_info_ = nullptr;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_HIT_TESTER_H_
