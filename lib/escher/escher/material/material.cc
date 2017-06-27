// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/material/material.h"

namespace escher {

Material::Material() : color_(vec3(1.f, 1.f, 1.f)) {}

void Material::SetTexture(TexturePtr texture) {
  texture_ = texture;
  if (texture) {
    image_view_ = texture_->image_view();
    sampler_ = texture_->sampler();
  } else {
    image_view_ = nullptr;
    sampler_ = nullptr;
  }
}

Material::~Material() {}

}  // namespace escher
