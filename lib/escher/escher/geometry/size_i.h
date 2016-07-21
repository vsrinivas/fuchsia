// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "escher/geometry/types.h"

#include "ftl/macros.h"

namespace escher {

class SizeI {
 public:
  SizeI();
  SizeI(int width, int height);

  int width() const { return width_; }
  int height() const { return height_; }

  float aspect_ratio() const {
    return float(width_) / height_;
  }

  int area() const { return width_ * height_; }

  vec2 AsVec2() const;
  bool Equals(const SizeI& size) const;
  size_t GetHashCode() const;

 private:
  int width_ = 0;
  int height_ = 0;
};

inline bool operator==(const SizeI& lhs, const SizeI& rhs) {
  return lhs.width() == rhs.width() && lhs.height() == rhs.height();
}

}  // namespace escher
