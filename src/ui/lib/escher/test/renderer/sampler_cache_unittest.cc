// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/renderer/sampler_cache.h"

#include "gtest/gtest.h"
#include "src/ui/lib/escher/renderer/render_queue_context.h"
#include "src/ui/lib/escher/test/gtest_escher.h"

namespace {
using namespace escher;

VK_TEST(SamplerCache, LazyCaching) {
  auto escher = test::GetEscher()->GetWeakPtr();

  SamplerCache cache(escher->resource_recycler()->GetWeakPtr());

  auto s1 = cache.ObtainSampler(vk::Filter::eNearest, true);
  auto s2 = cache.ObtainSampler(vk::Filter::eNearest, true);
  EXPECT_EQ(s1, s2);
  EXPECT_EQ(1U, cache.size());
  auto s3 = cache.ObtainSampler(vk::Filter::eNearest, false);
  auto s4 = cache.ObtainSampler(vk::Filter::eNearest, false);
  EXPECT_EQ(s3, s4);
  EXPECT_NE(s1, s3);
  EXPECT_EQ(2U, cache.size());
  auto s5 = cache.ObtainSampler(vk::Filter::eLinear, true);
  auto s6 = cache.ObtainSampler(vk::Filter::eLinear, true);
  EXPECT_EQ(s5, s6);
  EXPECT_NE(s1, s5);
  EXPECT_NE(s3, s5);
  EXPECT_EQ(3U, cache.size());
  auto s7 = cache.ObtainSampler(vk::Filter::eLinear, false);
  auto s8 = cache.ObtainSampler(vk::Filter::eLinear, false);
  EXPECT_EQ(s7, s8);
  EXPECT_NE(s1, s7);
  EXPECT_NE(s3, s7);
  EXPECT_NE(s5, s7);
  EXPECT_EQ(4U, cache.size());

  auto yuv1 = cache.ObtainYuvSampler(vk::Format::eG8B8G8R8422Unorm, vk::Filter::eLinear, true);
  auto yuv2 = cache.ObtainYuvSampler(vk::Format::eG8B8G8R8422Unorm, vk::Filter::eLinear, true);
  EXPECT_EQ(yuv1, yuv2);
  EXPECT_NE(yuv1, s1);
  EXPECT_NE(yuv1, s3);
  EXPECT_NE(yuv1, s4);
  EXPECT_NE(yuv1, s5);
  EXPECT_EQ(5U, cache.size());

  // From now on don't bother with EXPECT_NE()... verifying the expected cache size is good enough.
  cache.ObtainYuvSampler(vk::Format::eG8B8G8R8422Unorm, vk::Filter::eLinear, false);
  EXPECT_EQ(6U, cache.size());
  cache.ObtainYuvSampler(vk::Format::eG8B8G8R8422Unorm, vk::Filter::eNearest, true);
  EXPECT_EQ(7U, cache.size());
  cache.ObtainYuvSampler(vk::Format::eG8B8G8R8422Unorm, vk::Filter::eNearest, false);
  EXPECT_EQ(8U, cache.size());
  cache.ObtainYuvSampler(vk::Format::eG8B8R82Plane420Unorm, vk::Filter::eLinear, true);
  EXPECT_EQ(9U, cache.size());
  cache.ObtainYuvSampler(vk::Format::eG8B8R82Plane420Unorm, vk::Filter::eLinear, false);
  EXPECT_EQ(10U, cache.size());
  cache.ObtainYuvSampler(vk::Format::eG8B8R82Plane420Unorm, vk::Filter::eNearest, true);
  EXPECT_EQ(11U, cache.size());
  cache.ObtainYuvSampler(vk::Format::eG8B8R82Plane420Unorm, vk::Filter::eNearest, false);
  EXPECT_EQ(12U, cache.size());
}

}  // namespace
