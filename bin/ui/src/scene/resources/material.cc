// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene/resources/material.h"

#include "apps/mozart/src/scene/resources/image.h"
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

void Material::SetTexture(const ImagePtr& texture_image) {
  texture_ = texture_image;
  escher_material_->SetTexture(
      texture_image ? ftl::MakeRefCounted<escher::Texture>(
                          session()->context()->escher_resource_recycler(),
                          texture_image->escher_image(), vk::Filter::eLinear)
                    : escher::TexturePtr());
}

}  // namespace scene
}  // namespace mozart
