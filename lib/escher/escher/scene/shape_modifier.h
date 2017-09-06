// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "escher/util/debug_print.h"

namespace escher {

// Set of flags that specify modifications that should be made to a shape.
// The specified modifiers must be compatible with each other, and the mesh
// attribute layout (this is enforced by DCHECK, so go ahead and try).
enum class ShapeModifier {
  // Adds a sine-wave "wobble" to the shape's vertex shader.
  kWobble = 1,
};

using ShapeModifiers = vk::Flags<ShapeModifier>;

inline ShapeModifiers operator|(ShapeModifier bit0, ShapeModifier bit1) {
  return ShapeModifiers(bit0) | bit1;
}

inline ShapeModifiers operator~(ShapeModifier bit) {
  return ~ShapeModifiers(bit);
}

// Debugging.
ESCHER_DEBUG_PRINTABLE(ShapeModifier);
ESCHER_DEBUG_PRINTABLE(ShapeModifiers);

}  // namespace escher

namespace vk {

template<> struct FlagTraits<escher::ShapeModifier> {
  enum {
    allFlags = VkFlags(escher::ShapeModifier::kWobble)
  };
};

}  // namespace vk
