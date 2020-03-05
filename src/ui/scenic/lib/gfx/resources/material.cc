// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/material.h"

#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/resources/image.h"
#include "src/ui/scenic/lib/gfx/resources/image_base.h"
#include "src/ui/scenic/lib/gfx/resources/image_pipe.h"

namespace {

// TODO(SCN-1380): This is a hack that temporarily avoids memory and performance
// issues involving immutable samplers. It relies on the fact that Scenic is
// currently a singleton service with a single vk::Device, with no user-provided
// parameters that affect sampler construction other than image format. SCN-1380
// covers the real task to solve this problem, which involves design work both
// at the Scenic and Escher levels.
escher::SamplerPtr CreateNV12Sampler(escher::ResourceRecycler* recycler) {
  static vk::Device last_device;
  static escher::SamplerPtr last_sampler;

  if (recycler->vulkan_context().device == last_device && last_sampler)
    return last_sampler;

  if (last_sampler) {
    FXL_LOG(WARNING) << "YUV Sampler was not successfully cached, memory "
                        "footprint will increase.";
  }

  last_device = recycler->vulkan_context().device;
  last_sampler = fxl::MakeRefCounted<escher::Sampler>(recycler, vk::Format::eG8B8R82Plane420Unorm,
                                                      vk::Filter::eLinear);
  return last_sampler;
}

}  // namespace

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
}

void Material::SetTexture(ImageBasePtr texture_image) { texture_ = std::move(texture_image); }

void Material::UpdateEscherMaterial(escher::BatchGpuUploader* gpu_uploader,
                                    escher::ImageLayoutUpdater* layout_updater) {
  // Update our escher::Material if our texture's presented image changed.
  escher::ImagePtr escher_image;
  if (texture_) {
    texture_->UpdateEscherImage(gpu_uploader, layout_updater);
    escher_image = texture_->GetEscherImage();
  }
  const escher::TexturePtr& escher_texture = escher_material_->texture();

  if (!escher_texture || escher_image != escher_texture->image()) {
    escher::TexturePtr texture;
    if (escher_image) {
      auto recycler = resource_context().escher_resource_recycler;
      // TODO(SCN-1403): Technically, eG8B8R82Plane420Unorm is not enough to
      // assume NV12, but it's currently the only format we support at the
      // sampler level.
      if (escher_image->format() == vk::Format::eG8B8R82Plane420Unorm) {
        texture = fxl::MakeRefCounted<escher::Texture>(recycler, CreateNV12Sampler(recycler),
                                                       escher_image);
      } else {
        texture = escher::Texture::New(recycler, escher_image, vk::Filter::eLinear);
        // TODO(ES-199, ES-200): Reusing samplers is just good policy, but it is
        // required for immutable samplers because, until these bugs are fixed,
        // Escher will keep these samplers around forever.
        FXL_DCHECK(!texture->sampler()->is_immutable())
            << "Immutable samplers need to be cached to avoid unbounded memory "
               "consumption";
      }
    }
    escher_material_->SetTexture(std::move(texture));
  }
}

}  // namespace gfx
}  // namespace scenic_impl
