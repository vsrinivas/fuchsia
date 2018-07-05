// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/renderer/moment_shadow_map.h"

namespace escher {

const ShadowMapTypeInfo MomentShadowMap::kTypeInfo("MomentShadowMap",
                                                   ShadowMapType::kMoment);

MomentShadowMap::MomentShadowMap(const ImagePtr& image, const mat4& matrix,
                                 const vec3& light_color)
    : ShadowMap(image, matrix, light_color) {}

}  // namespace escher
