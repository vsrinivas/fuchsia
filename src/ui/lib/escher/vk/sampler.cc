// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/sampler.h"

#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/util/image_utils.h"

namespace escher {

const ResourceTypeInfo Sampler::kTypeInfo("Sampler", ResourceType::kResource,
                                          ResourceType::kSampler);

Sampler::Sampler(ResourceRecycler* resource_recycler, vk::Format format, vk::Filter filter,
                 bool use_unnormalized_coordinates)
    : Resource(resource_recycler), is_immutable_(false) {
  auto device = resource_recycler->vulkan_context().device;

  // TODO(fxbug.dev/24595): eG8B8R82Plane420Unorm/eG8B8G8R8422Unorm is not enough to assume NV12,
  // but they're currently the only formats we support at the sampler level.
  if (image_utils::IsYuvFormat(format)) {
    FX_DCHECK(resource_recycler->caps().allow_ycbcr);

    // Check chroma filter support.
    auto physical_device = resource_recycler->vulkan_context().physical_device;
    auto format_properties = physical_device.getFormatProperties(format);

    vk::SamplerYcbcrConversionCreateInfo ycbcr_create_info;
    ycbcr_create_info.pNext = nullptr;
    ycbcr_create_info.format = format;
    ycbcr_create_info.ycbcrModel = vk::SamplerYcbcrModelConversion::eYcbcr709;
    ycbcr_create_info.ycbcrRange = vk::SamplerYcbcrRange::eItuNarrow;
    ycbcr_create_info.components = {
        VK_COMPONENT_SWIZZLE_IDENTITY,  // R
        VK_COMPONENT_SWIZZLE_IDENTITY,  // G
        VK_COMPONENT_SWIZZLE_IDENTITY,  // B
        VK_COMPONENT_SWIZZLE_IDENTITY,  // A
    };
    if (format_properties.optimalTilingFeatures &
        vk::FormatFeatureFlagBits::eCositedChromaSamples) {
      ycbcr_create_info.xChromaOffset = vk::ChromaLocation::eCositedEven;
      ycbcr_create_info.yChromaOffset = vk::ChromaLocation::eCositedEven;
    } else if (format_properties.optimalTilingFeatures &
               vk::FormatFeatureFlagBits::eMidpointChromaSamples) {
      ycbcr_create_info.xChromaOffset = vk::ChromaLocation::eMidpoint;
      ycbcr_create_info.yChromaOffset = vk::ChromaLocation::eMidpoint;
    } else {
      FX_LOGS(ERROR) << "The potential features of format [" << vk::to_string(format) << "] don't "
                     << "support eCositedChromaSamples or eMidpointChromaSamples!";
      FX_NOTREACHED();
    }

    // If the chroma filter is not supported by the device, we would like to
    // make it fall back to eNearest for YUV images only, while still keeping
    // the choice of filter in VkSamplerCreateInfo for non-YUV images.
    if (filter == vk::Filter::eLinear &&
        !(format_properties.optimalTilingFeatures &
          vk::FormatFeatureFlagBits::eSampledImageYcbcrConversionLinearFilter)) {
      ycbcr_create_info.chromaFilter = vk::Filter::eNearest;
    } else {
      ycbcr_create_info.chromaFilter = filter;
    }
    ycbcr_create_info.forceExplicitReconstruction = VK_FALSE;

    ycbcr_conversion_ = ESCHER_CHECKED_VK_RESULT(device.createSamplerYcbcrConversionKHR(
        ycbcr_create_info, nullptr, resource_recycler->vulkan_context().loader));

    is_immutable_ = true;
  }

  vk::SamplerCreateInfo sampler_info;
  sampler_info.pNext = GetExtensionData();
  sampler_info.magFilter = filter;
  sampler_info.minFilter = filter;

  sampler_info.anisotropyEnable = false;
  sampler_info.maxAnisotropy = 1.0;
  sampler_info.unnormalizedCoordinates = use_unnormalized_coordinates;
  sampler_info.compareEnable = VK_FALSE;
  sampler_info.compareOp = vk::CompareOp::eAlways;
  sampler_info.mipLodBias = 0.0f;
  sampler_info.minLod = 0.0f;
  sampler_info.maxLod = 0.0f;
  if (use_unnormalized_coordinates) {
    sampler_info.mipmapMode = vk::SamplerMipmapMode::eNearest;
  } else {
    sampler_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
  }
  sampler_info.addressModeU = vk::SamplerAddressMode::eClampToEdge;
  sampler_info.addressModeV = vk::SamplerAddressMode::eClampToEdge;
  sampler_info.addressModeW = vk::SamplerAddressMode::eClampToEdge;

  sampler_ = ESCHER_CHECKED_VK_RESULT(vk_device().createSampler(sampler_info));
}

Sampler::~Sampler() {
  if (is_immutable_) {
    vk_device().destroySamplerYcbcrConversionKHR(ycbcr_conversion_.conversion, nullptr,
                                                 vulkan_context().loader);
  }
  vk_device().destroySampler(sampler_);
}

}  // namespace escher
