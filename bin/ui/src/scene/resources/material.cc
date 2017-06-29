// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene/resources/material.h"

#include "apps/mozart/src/scene/resources/image.h"
#include "apps/mozart/src/scene/resources/image_base.h"
#include "apps/mozart/src/scene/resources/image_pipe.h"
#include "apps/mozart/src/scene/session/session.h"

namespace mozart {
namespace scene {

const ResourceTypeInfo Material::kTypeInfo = {ResourceType::kMaterial,
                                              "Material"};

Material::Material(Session* session)
    : Resource(session, Material::kTypeInfo),
      escher_material_(ftl::MakeRefCounted<escher::Material>()) {}

void Material::SetColor(float red, float green, float blue, float alpha) {
  // TODO: need to add alpha into escher material
  escher_material_->set_color(escher::vec3(red, green, blue));
}

void Material::SetTexture(ImageBasePtr texture_image) {
  texture_ = std::move(texture_image);
}

void Material::UpdateEscherMaterial() {
  // Update our escher::Material if our texture's presented image changed.
  escher::ImagePtr escher_image;
  if (texture_) {
    escher_image = texture_->GetEscherImage();
  }
  const escher::TexturePtr& escher_texture = escher_material_->texture();

  if (!escher_texture || escher_image != escher_texture->image()) {
    escher::TexturePtr texture;
    if (escher_image) {
      texture = ftl::MakeRefCounted<escher::Texture>(
          session()->context()->escher_resource_recycler(), escher_image,
          vk::Filter::eLinear);
    }
    escher_material_->SetTexture(std::move(texture));
  }
}

}  // namespace scene
}  // namespace mozart
