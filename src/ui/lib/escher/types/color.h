// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_TYPES_COLOR_H_
#define SRC_UI_LIB_ESCHER_TYPES_COLOR_H_

#include <tuple>

#include "src/ui/lib/glm_workaround/glm_workaround.h"

namespace escher {

struct ColorRgba {
  constexpr ColorRgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) : r(r), g(g), b(b), a(a) {}
  // Construct from floats in range 0 - 1.
  static ColorRgba FromFloats(glm::vec4 rgba) {
    rgba *= 255.f;
    return ColorRgba(static_cast<uint8_t>(rgba.r), static_cast<uint8_t>(rgba.g),
                     static_cast<uint8_t>(rgba.b), static_cast<uint8_t>(rgba.a));
  }
  static ColorRgba FromFloats(float r, float g, float b, float a) {
    return ColorRgba::FromFloats(glm::vec4(r, g, b, a));
  }

  const uint8_t* bytes() const { return reinterpret_cast<const uint8_t*>(this); }

  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
};

struct ColorBgra {
  // Constructor is idiomatic RGBA, but memory layout is native BGRA.
  constexpr ColorBgra(uint8_t r, uint8_t g, uint8_t b, uint8_t a) : b(b), g(g), r(r), a(a) {}
  // Construct from floats in range 0 - 1.
  static ColorBgra FromFloats(glm::vec4 rgba) {
    rgba *= 255.f;
    return ColorBgra(static_cast<uint8_t>(rgba.r), static_cast<uint8_t>(rgba.g),
                     static_cast<uint8_t>(rgba.b), static_cast<uint8_t>(rgba.a));
  }
  static ColorBgra FromFloats(float r, float g, float b, float a) {
    return ColorBgra::FromFloats(glm::vec4(r, g, b, a));
  }

  const uint8_t* bytes() const { return reinterpret_cast<const uint8_t*>(this); }

  uint8_t b;
  uint8_t g;
  uint8_t r;
  uint8_t a;
};

// Color equality.
inline bool operator==(const ColorRgba& a, const ColorRgba& b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}
inline bool operator==(const ColorBgra& a, const ColorBgra& b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// Color ordering.  There is no guarantee that two colors will sort the same
// way when represented as different formats.  For example, if A and B are
// instances of ColorRgba and A < B, then if they are mapped to equivalent
// instances of ColorBgra A` and B`, it is possible that B` > A`.  The rationale
// is that although this guarantee would be easy to provide for RGBA and BGRA,
// it can't be provided for HSV without converting to something RGB-like.
inline bool operator<(const ColorRgba& a, const ColorRgba& b) {
  return std::tie(a.r, a.g, a.b, a.a) < std::tie(b.r, b.g, b.b, b.a);
}
inline bool operator<(const ColorBgra& a, const ColorBgra& b) {
  return std::tie(a.r, a.g, a.b, a.a) < std::tie(b.r, b.g, b.b, b.a);
}

// Color printing.
std::ostream& operator<<(std::ostream& os, const ColorRgba& c);
std::ostream& operator<<(std::ostream& os, const ColorBgra& c);

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_TYPES_COLOR_H_
