// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_SCENE_VIEWING_VOLUME_H_
#define LIB_ESCHER_SCENE_VIEWING_VOLUME_H_

#include <math.h>

#include "lib/escher/util/debug_print.h"

#include "lib/escher/geometry/bounding_box.h"

namespace escher {

class ViewingVolume {
 public:
  ViewingVolume();
  ViewingVolume(float width, float height, float top, float bottom);
  ~ViewingVolume();

  ViewingVolume CopyWith(float width, float height);

  float width() const { return width_; }
  float height() const { return height_; }
  float top() const { return top_; }
  float bottom() const { return bottom_; }
  float depth() const { return top_ - bottom_; }

  BoundingBox bounding_box() const {
    return BoundingBox(glm::vec3(0.f, 0.f, bottom_),
                       glm::vec3(width_, height_, top_));
  }

 private:
  float width_ = 0.0f;
  float height_ = 0.0f;
  float top_ = 0.0f;
  float bottom_ = 0.0f;
};

// Debugging.
ESCHER_DEBUG_PRINTABLE(ViewingVolume);

}  // namespace escher

#endif  // LIB_ESCHER_SCENE_VIEWING_VOLUME_H_
