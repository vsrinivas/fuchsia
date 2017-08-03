// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/material/material.h"

namespace escher {

Material::Material() = default;
Material::~Material() = default;

MaterialPtr Material::New(vec4 color, TexturePtr texture) {
  auto material = ftl::MakeRefCounted<Material>();
  material->set_color(color);
  material->SetTexture(std::move(texture));
  return material;
}

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

}  // namespace escher
