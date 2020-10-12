// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/renderer/sampler_cache.h"

#include <gtest/gtest.h>

#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/renderer/render_queue_context.h"
#include "src/ui/lib/escher/test/common/gtest_escher.h"

#include "vulkan/vulkan.hpp"

namespace {
using namespace escher;

namespace {

bool SupportsLinearFilter(Escher* escher, vk::Format format) {
  return static_cast<bool>(
      escher->vk_physical_device().getFormatProperties(format).optimalTilingFeatures &
      vk::FormatFeatureFlagBits::eSampledImageFilterLinear &
      vk::FormatFeatureFlagBits::eSampledImageYcbcrConversionLinearFilter);
}

}  // namespace

VK_TEST(SamplerCache, LazyCaching) {
  auto escher = test::GetEscher()->GetWeakPtr();

  SamplerCache cache(escher->resource_recycler()->GetWeakPtr());
  size_t expected_cache_size = 0u;

  auto s1 = cache.ObtainSampler(vk::Filter::eNearest, true);
  auto s2 = cache.ObtainSampler(vk::Filter::eNearest, true);
  EXPECT_EQ(s1, s2);
  EXPECT_EQ(++expected_cache_size, cache.size());
  auto s3 = cache.ObtainSampler(vk::Filter::eNearest, false);
  auto s4 = cache.ObtainSampler(vk::Filter::eNearest, false);
  EXPECT_EQ(s3, s4);
  EXPECT_NE(s1, s3);
  EXPECT_EQ(++expected_cache_size, cache.size());
  auto s5 = cache.ObtainSampler(vk::Filter::eLinear, true);
  auto s6 = cache.ObtainSampler(vk::Filter::eLinear, true);
  EXPECT_EQ(s5, s6);
  EXPECT_NE(s1, s5);
  EXPECT_NE(s3, s5);
  EXPECT_EQ(++expected_cache_size, cache.size());
  auto s7 = cache.ObtainSampler(vk::Filter::eLinear, false);
  auto s8 = cache.ObtainSampler(vk::Filter::eLinear, false);
  EXPECT_EQ(s7, s8);
  EXPECT_NE(s1, s7);
  EXPECT_NE(s3, s7);
  EXPECT_NE(s5, s7);
  EXPECT_EQ(++expected_cache_size, cache.size());

  if (escher->allow_ycbcr()) {
    vk::Format format = vk::Format::eG8B8G8R8422Unorm;
    if (impl::IsYuvConversionSupported(escher.get()->vk_physical_device(), format)) {
      // Use eNearest as the filter since it is supported by all platforms.
      auto yuv1 = cache.ObtainYuvSampler(format, vk::Filter::eNearest, true);
      auto yuv2 = cache.ObtainYuvSampler(format, vk::Filter::eNearest, true);
      EXPECT_EQ(yuv1, yuv2);
      EXPECT_NE(yuv1, s1);
      EXPECT_NE(yuv1, s3);
      EXPECT_NE(yuv1, s4);
      EXPECT_NE(yuv1, s5);
      EXPECT_EQ(++expected_cache_size, cache.size());

      // From now on don't bother with EXPECT_NE()... verifying the expected cache size is good
      // enough.
      cache.ObtainYuvSampler(format, vk::Filter::eNearest, false);
      EXPECT_EQ(++expected_cache_size, cache.size());
      if (SupportsLinearFilter(escher.get(), format)) {
        cache.ObtainYuvSampler(format, vk::Filter::eLinear, false);
        EXPECT_EQ(++expected_cache_size, cache.size());
      } else {
        FX_LOGS(INFO) << "Linear filtering of format " << vk::to_string(format)
                      << " is not supported by physical device. Skip testing obtaining"
                      << " sampler for linear filter.";
      }
    } else {
      FX_LOGS(INFO) << "YCbCr conversion of format " << vk::to_string(format)
                    << " is not supported by physical device. Skip testing obtaining sampler for "
                       "this format.";
    }

    format = vk::Format::eG8B8R82Plane420Unorm;
    if (impl::IsYuvConversionSupported(escher.get()->vk_physical_device(), format)) {
      cache.ObtainYuvSampler(format, vk::Filter::eNearest, true);
      EXPECT_EQ(++expected_cache_size, cache.size());
      cache.ObtainYuvSampler(format, vk::Filter::eNearest, false);
      EXPECT_EQ(++expected_cache_size, cache.size());
      if (SupportsLinearFilter(escher.get(), format)) {
        cache.ObtainYuvSampler(format, vk::Filter::eLinear, true);
        EXPECT_EQ(++expected_cache_size, cache.size());
        cache.ObtainYuvSampler(format, vk::Filter::eLinear, false);
        EXPECT_EQ(++expected_cache_size, cache.size());
      } else {
        FX_LOGS(INFO) << "Linear filtering of format " << vk::to_string(format)
                      << " is not supported by physical device. Skip testing obtaining"
                      << " sampler for linear filter.";
      }
    } else {
      FX_LOGS(INFO) << "YCbCr conversion of format " << vk::to_string(format)
                    << " is not supported by physical device. Skip testing obtaining sampler for "
                       "this format.";
    }
  }
}

}  // namespace
