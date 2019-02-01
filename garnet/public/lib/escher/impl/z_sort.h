// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_IMPL_Z_SORT_H_
#define LIB_ESCHER_IMPL_Z_SORT_H_

#include <algorithm>
#include <vector>

#include "lib/escher/scene/camera.h"
#include "lib/escher/scene/object.h"

namespace escher {
namespace impl {

float EstimateZTranslation(const Camera& camera, const mat4& object_transform);
// |camera_transform| - a matrix concatenating a camera's projection and view
//  matrices. The projection matrix may be omitted if it does not change the
//  orientation of the world.
float EstimateZTranslation(const mat4 camera_transform,
                           const mat4& object_transform);

// Returns |true| if object |a| is behind (has a greater z than) object |b|.
//
// |camera_desc| - either a |Camera| or a |mat4| concatenating a camera's
// projection and view matrices.
//
// TODO(rosswang): more sophisticated sorting is required for edge cases
template <class CameraDesc>
bool ZCompare(const CameraDesc& camera_desc, const Object& a, const Object& b) {
  return EstimateZTranslation(camera_desc, a.transform()) >
         EstimateZTranslation(camera_desc, b.transform());
}

template <class Index, class CameraDesc>
void ZSort(std::vector<Index>* indices, const std::vector<Object>& objects,
           const CameraDesc& camera_desc) {
  std::sort(indices->begin(), indices->end(),
            [&objects, &camera_desc](Index a, Index b) {
              return ZCompare(camera_desc, objects[a], objects[b]);
            });
}

}  // namespace impl
}  // namespace escher

#endif  // LIB_ESCHER_IMPL_Z_SORT_H_
