// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/renderer/shadow_map.h"

#include "lib/escher/escher.h"

namespace escher {

const ShadowMapTypeInfo ShadowMap::kTypeInfo("ShadowMap",
                                             ShadowMapType::kDefault);

ShadowMap::ShadowMap(ImagePtr image, glm::mat4 matrix, glm::vec3 light_color)
    : texture_(fxl::MakeRefCounted<Texture>(
          image->escher()->resource_recycler(), std::move(image),
          vk::Filter::eLinear, vk::ImageAspectFlagBits::eColor)),
      matrix_(matrix),
      light_color_(light_color) {}

ShadowMap::~ShadowMap() = default;

}  // namespace escher
