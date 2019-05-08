// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_HIT_H_
#define GARNET_LIB_UI_GFX_ENGINE_HIT_H_

#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/id.h"
#include "garnet/lib/ui/gfx/resources/nodes/node.h"
#include "src/ui/lib/escher/geometry/types.h"

namespace scenic_impl {
namespace gfx {

// Describes where a hit occurred within the content of a tagged node.
struct Hit {
  // The node's tag value. Non-zero values participate in the SessionHitTester.
  uint32_t tag_value;

  // Raw pointer to the node. Valid only in the context of Scenic itself.
  // NOTE: The consumer (input subsystem) must not retain these pointers!
  const Node* node;

  // The ray that was used to perform the hit test, in the hit node's coordinate
  // system.
  escher::ray4 ray;

  // The inverse transformation matrix which maps the coordinate system of
  // the hit node to the node at which the hit test was initiated.
  escher::mat4 inverse_transform;

  // The distance from the ray's origin to the closest point of intersection
  // in multiples of the ray's direction vector.  To compute the point of
  // intersection, multiply the ray's direction vector by |distance| and
  // add the ray's origin point.
  float distance;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_ENGINE_HIT_H_
