// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_GFX_COMPOSITOR_GRAPH_TRANSFORM_PAIR_H_
#define SERVICES_GFX_COMPOSITOR_GRAPH_TRANSFORM_PAIR_H_

#include "third_party/skia/include/core/SkMatrix44.h"

namespace compositor {

// Contains information about a transformation and its inverse.
// The inverse is computed lazily and cached.
class TransformPair {
 public:
  TransformPair(const SkMatrix44& forward);
  ~TransformPair();

  // Gets the forward transformation.
  const SkMatrix44& forward() const { return forward_; }

  // Gets the inverse transformation.
  const SkMatrix44& GetInverse() const;

  // Maps a point using the inverse transformation.
  const SkPoint InverseMapPoint(const SkPoint& point) const;

 private:
  const SkMatrix44 forward_;
  mutable SkMatrix44 cached_inverse_;
  mutable bool cached_inverse_valid_ = false;
};

}  // namespace compositor

#endif  // SERVICES_GFX_COMPOSITOR_GRAPH_TRANSFORM_PAIR_H_
