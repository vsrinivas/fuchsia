// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/flatland_types.h"

#include <glm/gtc/epsilon.hpp>

namespace flatland {

namespace {

std::ostream& operator<<(std::ostream& str, glm::ivec2 v) {
  return str << "(" << v.x << ", " << v.y << ")";
}

std::ostream& operator<<(std::ostream& str, fuchsia::ui::composition::Orientation orientation) {
  switch (orientation) {
    case fuchsia::ui::composition::Orientation::CCW_0_DEGREES:
      return str << "CCW_0_DEGREES";
    case fuchsia::ui::composition::Orientation::CCW_90_DEGREES:
      return str << "CCW_90_DEGREES";
    case fuchsia::ui::composition::Orientation::CCW_180_DEGREES:
      return str << "CCW_180_DEGREES";
    case fuchsia::ui::composition::Orientation::CCW_270_DEGREES:
      return str << "CCW_270_DEGREES";
  }
}

}  // namespace

std::ostream& operator<<(std::ostream& str, const flatland::ImageRect& r) {
  return str << "ImageRect[origin:" << r.origin << " extent:" << r.extent
             << " orientation:" << r.orientation << " texel_uvs:[" << r.texel_uvs[0] << ","
             << r.texel_uvs[1] << "," << r.texel_uvs[2] << "," << r.texel_uvs[3] << "]]";
}

bool ImageRect::operator==(const ImageRect& other) const {
  constexpr float kEpsilon = 0.001f;
  return glm::all(glm::epsilonEqual(origin, other.origin, kEpsilon)) &&
         glm::all(glm::epsilonEqual(extent, other.extent, kEpsilon)) &&
         orientation == other.orientation && texel_uvs[0] == other.texel_uvs[0] &&
         texel_uvs[1] == other.texel_uvs[1] && texel_uvs[2] == other.texel_uvs[2] &&
         texel_uvs[3] == other.texel_uvs[3];
}

}  // namespace flatland
