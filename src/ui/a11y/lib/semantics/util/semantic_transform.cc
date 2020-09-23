// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/util/semantic_transform.h"

namespace a11y {

void SemanticTransform::ChainLocalTransform(const fuchsia::ui::gfx::mat4& local_transform) {
  // Since we assume that local_transform has the form
  //
  // | Sx 0  0  Tx |
  // | 0  Sy 0  Ty |
  // | 0  0  Sz Tz |
  // | 0  0  0  1  |
  //
  // we can simplify the matrix multiplication.  The logic below is for computing
  // accumulator = local_transform * accumulator, when both matrices have this form.  Note
  // that in this case, the resulting matrix will always also be of this form.
  scale_vector_[0] *= local_transform.matrix[0];
  scale_vector_[1] *= local_transform.matrix[5];
  scale_vector_[2] *= local_transform.matrix[10];

  translation_vector_[0] =
      (local_transform.matrix[0] * translation_vector_[0]) + local_transform.matrix[12];
  translation_vector_[1] =
      (local_transform.matrix[5] * translation_vector_[1]) + local_transform.matrix[13];
  translation_vector_[2] =
      (local_transform.matrix[10] * translation_vector_[2]) + local_transform.matrix[14];
}

fuchsia::ui::gfx::vec3 SemanticTransform::Apply(const fuchsia::ui::gfx::vec3& point) const {
  fuchsia::ui::gfx::vec3 new_point;
  new_point.x = point.x * scale_vector_[0] + translation_vector_[0];
  new_point.y = point.y * scale_vector_[1] + translation_vector_[1];
  new_point.z = point.z * scale_vector_[2] + translation_vector_[2];
  return new_point;
}

SemanticTransform SemanticTransform::Invert() const {
  SemanticTransform new_transform;
  new_transform.scale_vector_ = {1.f / scale_vector_[0], 1.f / scale_vector_[1],
                                 1.f / scale_vector_[2]};
  new_transform.translation_vector_ = {-translation_vector_[0] / scale_vector_[0],
                                       -translation_vector_[1] / scale_vector_[1],
                                       -translation_vector_[2] / scale_vector_[2]};
  return new_transform;
}

}  // namespace a11y
