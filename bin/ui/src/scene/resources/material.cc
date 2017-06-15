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

Material::Material(Session* session,
                   float red,
                   float green,
                   float blue,
                   float alpha,
                   ImagePtr texture_image)
    : Material(session,
               texture_image,
               texture_image
                   ? ftl::MakeRefCounted<escher::Texture>(
                         session->context()->escher_resource_recycler(),
                         texture_image->escher_image(),
                         vk::Filter::eLinear)
                   : escher::TexturePtr(),
               red,
               green,
               blue,
               alpha) {}

Material::Material(Session* session,
                   ImagePtr image,
                   escher::TexturePtr escher_texture,
                   float red,
                   float green,
                   float blue,
                   float alpha)
    : Resource(session, Material::kTypeInfo),
      escher_material_(ftl::MakeRefCounted<escher::Material>(escher_texture)),
      texture_(image) {
  // TODO: need to add alpha into escher material
  escher_material_->set_color(escher::vec3(red, green, blue));
}

}  // namespace scene
}  // namespace mozart
