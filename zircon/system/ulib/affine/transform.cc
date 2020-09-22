// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/affine/transform.h>

namespace affine {

Transform Transform::Compose(const Transform& bc, const Transform& ab, Exact exact) {
  // TODO(fxbug.dev/13293)
  return Transform(ab.a_offset(), bc.Apply(ab.b_offset()),
                   Ratio::Product(ab.ratio(), bc.ratio(), exact));
}

}  // namespace affine
