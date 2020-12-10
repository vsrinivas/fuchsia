// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/texture.h"

#include "src/ui/lib/escher/impl/command_buffer.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/image.h"

namespace escher {

const ResourceTypeInfo Texture::kTypeInfo("Texture", ResourceType::kResource,
                                          ResourceType::kImageView, ResourceType::kTexture);

TexturePtr Texture::New(ResourceRecycler* resource_recycler, ImagePtr image, vk::Filter filter,
                        vk::ImageAspectFlags aspect_mask, bool use_unnormalized_coordinates) {
  if (!image) {
    return TexturePtr();
  }

  SamplerPtr sampler = fxl::MakeRefCounted<Sampler>(resource_recycler, image->format(), filter,
                                                    use_unnormalized_coordinates);

  if (sampler->is_immutable()) {
    FX_LOGS(WARNING) << "An immutable sampler was created using Texture::New. If "
                        "this happens over and over again, the system will likely OOM. "
                        "Build a separate immutable Sampler object and share it across "
                        "multiple Texture objects.";
  }

  return fxl::MakeRefCounted<Texture>(resource_recycler, std::move(sampler), image, aspect_mask);
}

Texture::Texture(ResourceRecycler* recycler, SamplerPtr sampler, ImagePtr image,
                 vk::ImageAspectFlags aspect_mask)
    : ImageView(recycler, image, aspect_mask, sampler->GetExtensionData()),
      sampler_(std::move(sampler)),
      is_yuv_format_(image_utils::IsYuvFormat(image->format())) {}

Texture::~Texture() {}

}  // namespace escher
