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
// are handled. The ray and the node should be in the same coordinate system (before applying the
// node's own transform).
void HitTest(Node* node, const escher::ray4& ray, HitAccumulator<NodeHit>* accumulator);

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_HIT_TESTER_H_
