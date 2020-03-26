// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/impl/descriptor_set_allocator.h"

#include "src/ui/lib/escher/test/common/gtest_escher.h"

namespace {
using namespace escher;

using DescriptorSetAllocatorTest = escher::test::TestWithVkValidationLayer;

VK_TEST_F(DescriptorSetAllocatorTest, General) {
  impl::DescriptorSetLayout layout = {};
  layout.sampled_image_mask = 0x5;
  layout.storage_image_mask = 0x2;
  layout.input_attachment_mask = 0x8;

  impl::DescriptorSetAllocator allocator(test::GetEscher()->vk_device(), layout);

  Hash hash1 = {1};
  Hash hash2 = {2};

  allocator.BeginFrame();
  std::pair<vk::DescriptorSet, bool> fr1_1 = allocator.Get(hash1);
  std::pair<vk::DescriptorSet, bool> fr1_1b = allocator.Get(hash1);
  std::pair<vk::DescriptorSet, bool> fr1_2 = allocator.Get(hash2);
  std::pair<vk::DescriptorSet, bool> fr1_2b = allocator.Get(hash2);

  // Only requests with the same hash value return the same descriptor set.
  EXPECT_EQ(fr1_1.first, fr1_1b.first);
  EXPECT_EQ(fr1_2.first, fr1_2b.first);
  EXPECT_NE(fr1_1.first, fr1_2.first);

  // The first request with a hash indicates that the descriptor set's contents
  // are invalid and must be written.  When a set is again requested for the
  // same hash, the allocator assumes that the previous caller wrote valid
  // descriptor values into the set.
  EXPECT_FALSE(fr1_1.second);
  EXPECT_TRUE(fr1_1b.second);
  EXPECT_FALSE(fr1_2.second);
  EXPECT_TRUE(fr1_2b.second);

  // Cached descriptor sets are available next frame.
  allocator.BeginFrame();
  std::pair<vk::DescriptorSet, bool> fr2_1 = allocator.Get(hash1);
  EXPECT_EQ(fr2_1.first, fr1_1.first);
  EXPECT_TRUE(fr2_1.second);

  // They're also still available if not used for an entire frame, but then
  // requested the next frame.
  allocator.BeginFrame();
  std::pair<vk::DescriptorSet, bool> fr3_2 = allocator.Get(hash2);
  EXPECT_EQ(fr3_2.first, fr1_2.first);
  EXPECT_TRUE(fr3_2.second);

  // However, if the hash isn't requested for four consecutive frames, then
  // requested again, then the resulting set's contents are invalid and must
  // be written.  In this case, there is no guarantee that the descriptor set
  // returned is the same one from 3 frames ago.
  allocator.BeginFrame();
  allocator.BeginFrame();
  allocator.BeginFrame();
  allocator.BeginFrame();
  std::pair<vk::DescriptorSet, bool> fr5_1 = allocator.Get(hash1);
  EXPECT_FALSE(fr5_1.second);

  // Of course, when re-requesting it in the same frame, it will be treated as
  // validly-cached.
  std::pair<vk::DescriptorSet, bool> fr5_1b = allocator.Get(hash1);
  EXPECT_EQ(fr5_1.first, fr5_1b.first);
  EXPECT_TRUE(fr5_1b.second);

  // During this test, there was a 62.5% cache hit rate.
  EXPECT_EQ(allocator.cache_hits(), 5U);
  EXPECT_EQ(allocator.cache_misses(), 3U);
}

}  // namespace
