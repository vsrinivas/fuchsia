// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/material.h"

#include "src/ui/lib/escher/renderer/sampler_cache.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/resources/image.h"
#include "src/ui/scenic/lib/gfx/resources/image_base.h"
#include "src/ui/scenic/lib/gfx/resources/image_pipe.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo Material::kTypeInfo = {ResourceType::kMaterial, "Material"};

Material::Material(Session* session, ResourceId id)
    : Resource(session, session->id(), id, Material::kTypeInfo),
      escher_material_(fxl::MakeRefCounted<escher::Material>()) {}

void Material::SetColor(float red, float green, float blue, float alpha) {
  escher_material_->set_color(escher::vec4(red, green, blue, alpha));
  // TODO(rosswang): This and related affordances are not enough to allow
  // transparent textures to work on opaque materials. It may be worthwhile to
  // surface the |opaque| flag on the Scenic client API to support this.
  escher_material_->set_type(alpha == 1 ? escher::Material::Type::kOpaque
                                        : escher::Material::Type::kTranslucent);
  color_is_set_ = true;
}

void Material::SetTexture(ImageBasePtr texture_image) {
  texture_ = std::move(texture_image);
  // Set initial state as transparent until an Image is set.
  if (!color_is_set_) {
    escher_material_->set_color(escher::vec4(1, 1, 1, 0));
    escher_material_->set_type(escher::Material::Type::kTranslucent);
  }
}

void Material::UpdateEscherMaterial(escher::BatchGpuUploader* gpu_uploader,
                                    escher::ImageLayoutUpdater* layout_updater) {
  // Update our escher::Material if our texture's presented image changed.
  escher::ImagePtr escher_image;
  if (texture_) {
    texture_->UpdateEscherImage(gpu_uploader, layout_updater);
    escher_image = texture_->GetEscherImage();
    // Switch to a default opaque material after we have an Image.
    if (escher_image && !color_is_set_) {
      escher_material_->set_color(escher::vec4(1, 1, 1, 1));
      escher_material_->set_type(escher::Material::Type::kOpaque);
    }
  }
  const escher::TexturePtr& old_escher_texture = escher_material_->texture();

  if (!old_escher_texture || escher_image != old_escher_texture->image()) {
    escher::TexturePtr new_escher_texture;
    if (escher_image) {
      escher::SamplerPtr sampler;

      if (escher::image_utils::IsYuvFormat(escher_image->format())) {
        // TODO(fxbug.dev/7288, fxbug.dev/7289): Reusing samplers is just good policy, but it is a necessity for
        // immutable samplers, because allocating duplicate samplers will result in creation of
        // duplicate pipelines, descriptor set allocators.
        // NOTE: the previous comment said that until fxbug.dev/7288, fxbug.dev/7289 are fixed "Escher will keep
        // these samplers around forever."  Not quite sure what this means... is it because the
        // pipelines hang on to the sampler?  If so, that's bad, but generation tons of redundant
        // pipelines is worse (both for FPS and OOMing).
        sampler = resource_context().escher_sampler_cache->ObtainYuvSampler(escher_image->format(),
                                                                            vk::Filter::eLinear);
        FX_DCHECK(sampler->is_immutable());

      } else {
        sampler = resource_context().escher_sampler_cache->ObtainSampler(vk::Filter::eLinear);
        FX_DCHECK(!sampler->is_immutable());  // Just checking our expectation.
      }

      new_escher_texture = fxl::MakeRefCounted<escher::Texture>(
          resource_context().escher_resource_recycler, sampler, escher_image);
    }
    escher_material_->SetTexture(std::move(new_escher_texture));
  }
}

}  // namespace gfx
}  // namespace scenic_impl
