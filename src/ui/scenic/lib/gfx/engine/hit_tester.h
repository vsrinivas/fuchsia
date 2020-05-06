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
void HitTest(Node* starting_node, const escher::ray4& world_space_ray,
             HitAccumulator<NodeHit>* accumulator);

// Takes a screen space point and a layer stack, and performs a hit test in the Z-direction on all
// layers in |layer_stack|.
void PerformGlobalHitTest(const gfx::LayerStackPtr& layer_stack,
                          const glm::vec2& screen_space_coords,
                          gfx::HitAccumulator<gfx::ViewHit>* accumulator);

// Creates a ray in the Z-direction (pointing into the screen) at |screen_space_coords|.
escher::ray4 CreateScreenPerpendicularRay(glm::vec2 screen_space_coords);

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_HIT_TESTER_H_
