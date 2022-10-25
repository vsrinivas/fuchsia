// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_TYPES_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_TYPES_H_

#include <fuchsia/ui/composition/cpp/fidl.h>

#include <array>

#include <glm/glm.hpp>

namespace flatland {

// Represents an image rectangle, parameterized by an origin point, an extent representing the width
// and height. The texel UV coordinates specify, in clockwise order, the unnormalized clockwise
// texel coordinates beginning at the top-left coordinate (in texture-space). The orientation
// specifies the rotation applied to the rect. Note that origin and extent are specified in the
// new global coordinate-space (i.e. after all transforms have been applied).
struct ImageRect {
  ImageRect(const glm::vec2& origin, const glm::vec2& extent, const std::array<glm::ivec2, 4> uvs,
            fuchsia::ui::composition::Orientation orientation)
      : origin(origin), extent(extent), texel_uvs(uvs), orientation(orientation) {}

  // Creates an ImageRect with the specified width and height. |texel_uvs| are initialized using the
  // specified |extent| of the rectangle. Note that this may not be equal to the image you are
  // sampling from.
  ImageRect(const glm::vec2& origin, const glm::vec2& extent)
      : origin(origin),
        extent(extent),
        orientation(fuchsia::ui::composition::Orientation::CCW_0_DEGREES) {
    texel_uvs = {glm::vec2(0, 0), glm::vec2(extent.x, 0), glm::vec2(extent.x, extent.y),
                 glm::vec2(0, extent.y)};
  }

  ImageRect() = default;

  glm::vec2 origin = glm::vec2(0, 0);
  glm::vec2 extent = glm::vec2(1, 1);
  std::array<glm::ivec2, 4> texel_uvs = {glm::ivec2(0, 0), glm::ivec2(1, 0), glm::ivec2(1, 1),
                                         glm::ivec2(0, 1)};
  fuchsia::ui::composition::Orientation orientation;

  bool operator==(const ImageRect& other) const;
};

std::ostream& operator<<(std::ostream& str, const flatland::ImageRect& r);

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_TYPES_H_
