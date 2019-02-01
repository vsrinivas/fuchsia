// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_SHAPE_MODIFIER_WOBBLE_H_
#define LIB_ESCHER_SHAPE_MODIFIER_WOBBLE_H_

#include "lib/escher/scene/shape.h"

namespace escher {

// Data corresponding to ShapeModifier::kWobble.  Used with
// Object::shape_modifier_data() and Object::set_shape_modifier_data().
struct ModifierWobble {
  static const ShapeModifier kType = ShapeModifier::kWobble;

  struct SineParams {
    float speed = 0.f;
    float amplitude = 0.f;
    float frequency = 0.f;
  };
  static constexpr uint32_t kNumSineParams = 3;

  SineParams params[kNumSineParams];
};

}  // namespace escher

#endif  // LIB_ESCHER_SHAPE_MODIFIER_WOBBLE_H_
