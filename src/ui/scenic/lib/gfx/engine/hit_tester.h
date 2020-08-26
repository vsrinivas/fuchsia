// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_HIT_TESTER_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_HIT_TESTER_H_

#include "src/ui/scenic/lib/gfx/engine/hit_accumulator.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/node.h"

namespace scenic_impl {
namespace gfx {

// Performs a hit test on the contents and subtree of a node along the specified ray, adding hit
// candidates to the given accumulator. The accumulator determines which hits are kept and how they
// are handled. The ray should be in World Space.
// If |semantic_hit_test| is false, then a normal graphics mode hit test is performed. If
// |semantic_hit_test| is true, then a semantic (accessibility) hit test is performed, which
// honors the semantic visibility property of some nodes.
void HitTest(Node* starting_node, const escher::ray4& world_space_ray,
             HitAccumulator<NodeHit>* accumulator, bool semantic_hit_test);

// Convenience function that takes a ViewHit accumulator instead of a NodeHit accumulator.
void HitTest(Node* starting_node, const escher::ray4& world_space_ray,
             HitAccumulator<ViewHit>* accumulator, bool semantic_hit_test);

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_HIT_TESTER_H_
