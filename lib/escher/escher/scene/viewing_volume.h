// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <glm/glm.hpp>
#include <math.h>

#include "escher/geometry/size_i.h"

namespace escher {

class ViewingVolume {
 public:
  ViewingVolume();
  ViewingVolume(float width, float height, float near, float far);
  ~ViewingVolume();

  ViewingVolume CopyWith(float width, float height);

  float width() const { return width_; }
  float height() const { return height_; }
  float near() const { return near_; }
  float far() const { return far_; }

  float depth_range() const { return std::abs(near_ - far_); }

  mat4 GetProjectionMatrix() const;

 private:
  float width_ = 0.0f;
  float height_ = 0.0f;
  float near_ = 0.0f;
  float far_ = 0.0f;
};

}  // namespace escher
