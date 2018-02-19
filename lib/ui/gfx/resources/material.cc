// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/material.h"

#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/resources/image.h"
#include "garnet/lib/ui/gfx/resources/image_base.h"
#include "garnet/lib/ui/gfx/resources/image_pipe.h"

namespace scenic {
namespace gfx {

const ResourceTypeInfo Material::kTypeInfo = {ResourceType::kMaterial,
                                              "Material"};

Material::Material(Session* session, scenic::ResourceId id)
    : Resource(session, id, Material::kTypeInfo),
      escher_material_(fxl::MakeRefCounted<escher::Material>()) {}

void Material::SetColor(float red, float green, float blue, float alpha) {
  escher_material_->set_color(escher::vec4(red, green, blue, alpha));
  // TODO(rosswang): This and related affordances are not enough to allow
  // transparent textures to work on opaque materials. It may be worthwhile to
  // surface the |opaque| flag on the Scenic client API to support this.
  escher_material_->set_opaque(alpha == 1);
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
      texture = fxl::MakeRefCounted<escher::Texture>(
          session()->engine()->escher_resource_recycler(), escher_image,
          vk::Filter::eLinear);
    }
    escher_material_->SetTexture(std::move(texture));
  }
}

}  // namespace gfx
}  // namespace scenic
