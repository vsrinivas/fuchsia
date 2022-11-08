// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/utils/shader_warmup.h"

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/renderer/sampler_cache.h"

namespace utils {

// Helper for ImmutableSamplersForShaderWarmup().
static bool FilterSupportsOptimalTilingForFormat(vk::PhysicalDevice physical_device,
                                                 vk::Filter filter, vk::Format format) {
  vk::FormatFeatureFlagBits feature_flag;
  switch (filter) {
    case vk::Filter::eNearest:
      // eNearest filtering doesn't require a specific feature flag.
      return true;
    case vk::Filter::eLinear:
      feature_flag = vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
      break;
    case vk::Filter::eCubicEXT:
      // eCubicEXT and eCubicIMG are the same (and have the same value).
      static_assert(vk::Filter::eCubicEXT == vk::Filter::eCubicIMG, "");
      feature_flag = vk::FormatFeatureFlagBits::eSampledImageFilterCubicEXT;
      break;
  }
  bool has_support = static_cast<bool>(
      physical_device.getFormatProperties(format).optimalTilingFeatures & feature_flag);

  if (!has_support) {
    FX_LOGS(WARNING) << "Optimal tiling not supported for format=" << vk::to_string(format)
                     << " filter=" << vk::to_string(filter)
                     << ".  Skipping creating immutable sampler.";
  }
  return has_support;
}

std::vector<escher::SamplerPtr> ImmutableSamplersForShaderWarmup(escher::EscherWeakPtr escher,
                                                                 vk::Filter filter) {
  if (!escher->allow_ycbcr()) {
    return {};
  }

  // Generate the list of immutable samples for all of the YUV types that we expect to see.
  std::vector<escher::SamplerPtr> immutable_samplers;
  const std::vector<vk::Format> immutable_sampler_formats{vk::Format::eG8B8G8R8422Unorm,
                                                          vk::Format::eG8B8R82Plane420Unorm,
                                                          vk::Format::eG8B8R83Plane420Unorm};
  const std::vector<escher::ColorSpace> color_spaces{
      escher::ColorSpace::kRec709,
      escher::ColorSpace::kRec601Ntsc,
  };
  const auto vk_physical_device = escher->vk_physical_device();
  for (auto fmt : immutable_sampler_formats) {
    for (auto color_space : color_spaces) {
      if (escher::impl::IsYuvConversionSupported(vk_physical_device, fmt)) {
        if (FilterSupportsOptimalTilingForFormat(vk_physical_device, filter, fmt)) {
          immutable_samplers.push_back(
              escher->sampler_cache()->ObtainYuvSampler(fmt, filter, color_space));
        }
      } else {
        FX_LOGS(WARNING) << "YUV conversion not supported for format=" << vk::to_string(fmt)
                         << ".  Skipping creating immutable sampler.";
      }
    }
  }

  return immutable_samplers;
}

}  // namespace utils
