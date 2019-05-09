// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/command_buffer.h"

#include "src/ui/lib/escher/test/gtest_escher.h"
#include "src/ui/lib/escher/test/vk/vulkan_tester.h"

namespace {
using namespace escher;

class CommandBufferTest : public ::testing::Test, public VulkanTester {
  void SetUp() override { EXPECT_TRUE(test::GetEscher()->Cleanup()); }

  void TearDown() override {
    auto escher = test::GetEscher();
    escher->vk_device().waitIdle();
    EXPECT_TRUE(escher->Cleanup());
  }
};

// Smoke-test for getting/clearing dirtiness of CommandBuffer state,
// specifically:
// - a newly-created CommandBuffer initially has all dirty bits set.
// - GetAndClearDirty() both obtains the right values, and doesn't stomp other
//   values as it clears the previously-dirty ones.
//
// TODO(ES-83): this could be extended to test the following:
// - that various state-setters (e.g. SetCullMode()) dirty the correct bits
// - that GetAndClearDirty() can be used for multiple bits simultaneously.
VK_TEST_F(CommandBufferTest, Dirtyness) {
  auto escher = test::GetEscher();
  auto cb = CommandBuffer::NewForGraphics(escher);

  EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyStaticStateBit),
            DirtyBits::kDirtyStaticStateBit);
  EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyPipelineBit),
            DirtyBits::kDirtyPipelineBit);
  EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyViewportBit),
            DirtyBits::kDirtyViewportBit);
  EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyScissorBit),
            DirtyBits::kDirtyScissorBit);
  EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyDepthBiasBit),
            DirtyBits::kDirtyDepthBiasBit);
  EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyStencilMasksAndReferenceBit),
            DirtyBits::kDirtyStencilMasksAndReferenceBit);
  EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyStaticVertexBit),
            DirtyBits::kDirtyStaticVertexBit);
  EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyPushConstantsBit),
            DirtyBits::kDirtyPushConstantsBit);
  EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyDynamicBits),
            DirtyBits::kDirtyDynamicBits);

  // GetAndClearDirty() is the same as GetDirty(), except that the values are
  // also cleared.
  EXPECT_EQ(GetAndClearDirty(cb, DirtyBits::kDirtyStaticStateBit),
            DirtyBits::kDirtyStaticStateBit);
  EXPECT_EQ(GetAndClearDirty(cb, DirtyBits::kDirtyPipelineBit),
            DirtyBits::kDirtyPipelineBit);
  EXPECT_EQ(GetAndClearDirty(cb, DirtyBits::kDirtyViewportBit),
            DirtyBits::kDirtyViewportBit);
  EXPECT_EQ(GetAndClearDirty(cb, DirtyBits::kDirtyScissorBit),
            DirtyBits::kDirtyScissorBit);
  EXPECT_EQ(GetAndClearDirty(cb, DirtyBits::kDirtyDepthBiasBit),
            DirtyBits::kDirtyDepthBiasBit);
  EXPECT_EQ(GetAndClearDirty(cb, DirtyBits::kDirtyStencilMasksAndReferenceBit),
            DirtyBits::kDirtyStencilMasksAndReferenceBit);
  EXPECT_EQ(GetAndClearDirty(cb, DirtyBits::kDirtyStaticVertexBit),
            DirtyBits::kDirtyStaticVertexBit);
  EXPECT_EQ(GetAndClearDirty(cb, DirtyBits::kDirtyPushConstantsBit),
            DirtyBits::kDirtyPushConstantsBit);
  // All of these bits were already cleared individually above.
  EXPECT_EQ(GetAndClearDirty(cb, DirtyBits::kDirtyDynamicBits), 0u);

  // All bits were cleared above.
  EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyStaticStateBit), 0u);
  EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyPipelineBit), 0u);
  EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyViewportBit), 0u);
  EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyScissorBit), 0u);
  EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyDepthBiasBit), 0u);
  EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyStencilMasksAndReferenceBit), 0u);
  EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyStaticVertexBit), 0u);
  EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyPushConstantsBit), 0u);
  EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyDynamicBits), 0u);

  // TODO(ES-83): ideally only submitted CommandBuffers would need to be cleaned
  // up: if a never-submitted CB is destroyed, then it shouldn't keep anything
  // alive, and it shouldn't cause problems in e.g. CommandBufferPool due to a
  // forever-straggling buffer.
  EXPECT_TRUE(cb->Submit(nullptr));
}

// Smoke-test for CommandBufferPipelineState's bit-packing setters/getters.
VK_TEST_F(CommandBufferTest, StaticStateSetting) {
  auto escher = test::GetEscher();
  auto cb = CommandBuffer::NewForGraphics(escher);
  auto static_state = VulkanTester::GetStaticState(cb);

  {
    using vk::CompareOp;
    for (auto& op : std::vector<CompareOp>{
             CompareOp::eNever, CompareOp::eLess, CompareOp::eEqual,
             CompareOp::eLessOrEqual, CompareOp::eGreater, CompareOp::eNotEqual,
             CompareOp::eGreaterOrEqual, CompareOp::eAlways}) {
      cb->SetDepthCompareOp(op);
      EXPECT_EQ(static_state->get_depth_compare(), op);
      EXPECT_EQ(vk::CompareOp(static_state->depth_compare), op);
      EXPECT_EQ(GetAndClearDirty(cb, DirtyBits::kDirtyStaticStateBit),
                DirtyBits::kDirtyStaticStateBit);
      EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyStaticStateBit), 0u);
    }
  }

  // TODO(ES-83): ideally only submitted CommandBuffers would need to be cleaned
  // up: if a never-submitted CB is destroyed, then it shouldn't keep anything
  // alive, and it shouldn't cause problems in e.g. CommandBufferPool due to a
  // forever-straggling buffer.
  EXPECT_TRUE(cb->Submit(nullptr));
}

}  // namespace
