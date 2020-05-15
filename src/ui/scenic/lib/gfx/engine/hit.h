// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_HIT_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_HIT_H_

#include <zircon/types.h>

#include "src/ui/scenic/lib/gfx/resources/nodes/node.h"

namespace scenic_impl {
namespace gfx {

// Describes where a hit occurred within the content of a tagged node.
struct NodeHit {
  // Raw pointer to the hit node. Valid only in the context of Scenic itself.
  // NOTE: The consumer (input subsystem) must not retain these pointers!
  const Node* node = nullptr;

  // The distance from the ray's origin to the closest point of intersection in multiples of the
  // ray's direction vector. As the ray may be transformed during hit testing and in accumulator
  // chains, expressing distance in terms of the hit ray provides a consistent basis that allows hit
  // distances from different node coordinate systems to be compared.
  float distance = 0;
};

// Describes which view was hit and the distance to the view.
struct ViewHit {
  // The view_ref_koid of the view containing the geometry that was hit.
  zx_koid_t view_ref_koid;

  // The distance from the ray's origin to the closest point of intersection in multiples of the
  // ray's direction vector. As the ray may be transformed during hit testing and in accumulator
  // chains, expressing distance in terms of the hit ray provides a consistent basis that allows hit
  // distances from different node coordinate systems to be compared.
  float distance = 0;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_HIT_H_
