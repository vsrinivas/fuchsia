// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_HIT_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_HIT_H_

#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/node.h"
#include "src/ui/scenic/lib/gfx/resources/view.h"
#include "src/ui/scenic/lib/scheduling/id.h"

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

// Describes where a hit occurred within the content of a view.
struct ViewHit {
  // The view containing the geometry that was hit. Care should be taken not to hold onto this
  // pointer or this hit struct longer than necessary.
  ViewPtr view;

  // The transformation matrix that maps the coordinate system of the hit ray to the coordinate
  // system of the view. This in conjunction with |distance| defines the hit coordinate in view
  // coordinates.
  escher::mat4 transform;

  // The distance from the ray's origin to the closest point of intersection in multiples of the
  // ray's direction vector. As the ray may be transformed during hit testing and in accumulator
  // chains, expressing distance in terms of the hit ray provides a consistent basis that allows hit
  // distances from different node coordinate systems to be compared.
  float distance = 0;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_HIT_H_
