// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/test/gtest_escher.h"
#include "src/ui/lib/escher/third_party/granite/vk/shader_utils.h"

namespace {
using namespace escher;
using namespace impl;

using PushConstantRangeTest = test::TestWithVkValidationLayer;

//  Test a single push constant range. It should come back as is.
VK_TEST_F(PushConstantRangeTest, SingleRange) {
  vk::PushConstantRange range({}, 0, 50);
  std::vector<vk::PushConstantRange> array = {range};
  auto result = ConsolidatePushConstantRanges(array);
  EXPECT_EQ(result[0], range);
}

// Check two push constant ranges with no overlap. The result should
// be the same exact two ranges that were input.
VK_TEST_F(PushConstantRangeTest, NoOverlap) {
  vk::PushConstantRange range1(vk::ShaderStageFlagBits::eVertex, 0, 50);
  vk::PushConstantRange range2(vk::ShaderStageFlagBits::eFragment, 60, 100);

  std::vector<vk::PushConstantRange> array = {range1, range2};
  auto result = ConsolidatePushConstantRanges(array);
  EXPECT_EQ(result.size(), 2U);
  EXPECT_EQ(result[0], range1);
  EXPECT_EQ(result[1], range2) << result[1].offset << " " << result[1].size << " "
                               << to_string(result[1].stageFlags);
}

// Check two push constant ranges that do overlap. The result should
// be a single push constant range with 2 shader stages.
//
// First range goes from [0,50] and the second range goes from [40, 140]
// so the final output should have range [0, 140].
VK_TEST_F(PushConstantRangeTest, TwoOverlapping) {
  vk::PushConstantRange range1(vk::ShaderStageFlagBits::eVertex, 0, 50);
  vk::PushConstantRange range2(vk::ShaderStageFlagBits::eFragment, 40, 100);

  std::vector<vk::PushConstantRange> array = {range1, range2};
  auto result = ConsolidatePushConstantRanges(array);
  EXPECT_EQ(result.size(), 1U);
  EXPECT_EQ(result[0].offset, 0U);
  EXPECT_EQ(result[0].size, 140U);
  EXPECT_EQ(result[0].stageFlags,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);
}

// This unit test contains a range that completely encapsulates another range.
// The end result should be a single range with two shader stages whose offset
// and size match the outer range.
VK_TEST_F(PushConstantRangeTest, WhollyContainedRange) {
  vk::PushConstantRange range1(vk::ShaderStageFlagBits::eVertex, 0, 100);
  vk::PushConstantRange range2(vk::ShaderStageFlagBits::eFragment, 40, 50);

  std::vector<vk::PushConstantRange> array = {range1, range2};
  auto result = ConsolidatePushConstantRanges(array);
  EXPECT_EQ(result.size(), 1U);
  EXPECT_EQ(result[0].offset, 0U);
  EXPECT_EQ(result[0].size, 100U);
  EXPECT_EQ(result[0].stageFlags,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);
}

// Two ranges that are adjacent, meaning that range1 ends exactly where range2
// begins should be considered two different ranges. For example if range one
// goes from (0,60) and range two goes from (60, 100), those are two ranges.
VK_TEST_F(PushConstantRangeTest, AdjacentRanges) {
  vk::PushConstantRange range1(vk::ShaderStageFlagBits::eVertex, 0, 60);
  vk::PushConstantRange range2(vk::ShaderStageFlagBits::eFragment, 60, 50);

  std::vector<vk::PushConstantRange> array = {range1, range2};
  auto result = ConsolidatePushConstantRanges(array);
  EXPECT_EQ(result.size(), 2U);
  EXPECT_EQ(result[0].offset, 0U);
  EXPECT_EQ(result[0].size, 60U);
  EXPECT_EQ(result[1].offset, 60U);
  EXPECT_EQ(result[1].size, 50U);
}

// Check multiple ranges that span vertex, fragment and compute shaders.
// Order is given randomly in order to test that sorting works as well.
VK_TEST_F(PushConstantRangeTest, MultipleRanges) {
  vk::PushConstantRange range1(vk::ShaderStageFlagBits::eFragment, 40, 40);
  vk::PushConstantRange range2(vk::ShaderStageFlagBits::eVertex, 0, 50);
  vk::PushConstantRange range3(vk::ShaderStageFlagBits::eCompute, 80, 40);
  vk::PushConstantRange range4(vk::ShaderStageFlagBits::eFragment, 100, 10);
  vk::PushConstantRange range5(vk::ShaderStageFlagBits::eCompute, 90, 10);

  std::vector<vk::PushConstantRange> array = {range1, range2, range3, range4, range5};
  auto result = ConsolidatePushConstantRanges(array);
  EXPECT_EQ(result.size(), 2U);
  EXPECT_EQ(result[0].offset, 0U);
  EXPECT_EQ(result[0].size, 80U);
  EXPECT_EQ(result[0].stageFlags,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);

  EXPECT_EQ(result[1].offset, 80U);
  EXPECT_EQ(result[1].size, 40U);
  EXPECT_EQ(result[1].stageFlags,
            vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eFragment);
}

}  // anonymous namespace
