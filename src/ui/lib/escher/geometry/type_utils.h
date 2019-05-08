// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_GEOMETRY_TYPE_UTILS_H_
#define SRC_UI_LIB_ESCHER_GEOMETRY_TYPE_UTILS_H_

#include "src/ui/lib/escher/geometry/types.h"

namespace escher {

// Extend |v| to be a homogeneous vec4.  The w-component is set to |w|,
// which should be 0 for vectors and 1 for points.
inline vec4 Homo4(const vec3& v, float w) { return vec4(v, w); }
inline vec4 Homo4(const vec2& v, float w) { return vec4(v, 0, w); }

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_GEOMETRY_TYPE_UTILS_H_
