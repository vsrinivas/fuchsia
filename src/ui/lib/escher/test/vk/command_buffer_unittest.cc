// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/command_buffer.h"

#include "src/ui/lib/escher/test/common/gtest_escher.h"
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
// TODO(fxbug.dev/7174): this could be extended to test the following:
// - that various state-setters (e.g. SetCullMode()) dirty the correct bits
// - that GetAndClearDirty() can be used for multiple bits simultaneously.
VK_TEST_F(CommandBufferTest, Dirtyness) {
  auto escher = test::GetEscher();
  auto cb = CommandBuffer::NewForGraphics(escher, false);

  EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyStaticStateBit), DirtyBits::kDirtyStaticStateBit);
  EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyPipelineBit), DirtyBits::kDirtyPipelineBit);
  EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyViewportBit), DirtyBits::kDirtyViewportBit);
  EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyScissorBit), DirtyBits::kDirtyScissorBit);
  EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyDepthBiasBit), DirtyBits::kDirtyDepthBiasBit);
  EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyStencilMasksAndReferenceBit),
            DirtyBits::kDirtyStencilMasksAndReferenceBit);
  EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyStaticVertexBit), DirtyBits::kDirtyStaticVertexBit);
  EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyPushConstantsBit), DirtyBits::kDirtyPushConstantsBit);
  EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyDynamicBits), DirtyBits::kDirtyDynamicBits);

  // GetAndClearDirty() is the same as GetDirty(), except that the values are
  // also cleared.
  EXPECT_EQ(GetAndClearDirty(cb, DirtyBits::kDirtyStaticStateBit), DirtyBits::kDirtyStaticStateBit);
  EXPECT_EQ(GetAndClearDirty(cb, DirtyBits::kDirtyPipelineBit), DirtyBits::kDirtyPipelineBit);
  EXPECT_EQ(GetAndClearDirty(cb, DirtyBits::kDirtyViewportBit), DirtyBits::kDirtyViewportBit);
  EXPECT_EQ(GetAndClearDirty(cb, DirtyBits::kDirtyScissorBit), DirtyBits::kDirtyScissorBit);
  EXPECT_EQ(GetAndClearDirty(cb, DirtyBits::kDirtyDepthBiasBit), DirtyBits::kDirtyDepthBiasBit);
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

  // TODO(fxbug.dev/7174): ideally only submitted CommandBuffers would need to be cleaned
  // up: if a never-submitted CB is destroyed, then it shouldn't keep anything
  // alive, and it shouldn't cause problems in e.g. CommandBufferPool due to a
  // forever-straggling buffer.
  EXPECT_TRUE(cb->Submit(nullptr));
}

// Smoke-test for CommandBufferPipelineState's bit-packing setters/getters.
VK_TEST_F(CommandBufferTest, StaticStateSetting) {
  auto escher = test::GetEscher();
  auto cb = CommandBuffer::NewForGraphics(escher, false);
  auto static_state = VulkanTester::GetStaticState(cb);

  {
    using vk::CompareOp;
    for (auto& op :
         std::vector<CompareOp>{CompareOp::eNever, CompareOp::eLess, CompareOp::eEqual,
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

  // TODO(fxbug.dev/7174): ideally only submitted CommandBuffers would need to be cleaned
  // up: if a never-submitted CB is destroyed, then it shouldn't keep anything
  // alive, and it shouldn't cause problems in e.g. CommandBufferPool due to a
  // forever-straggling buffer.
  EXPECT_TRUE(cb->Submit(nullptr));
}

// CommandBuffer and CommandBufferPipelineState have matching setters for static state;
// the only difference is that the CommandBuffer variants set dirty bits if any changes are
// made.  This verifies that calling either variant produces the same change in the
// CommandBufferPipelineState::StaticState.
VK_TEST_F(CommandBufferTest, StaticStateSettingMatches) {
  auto escher = test::GetEscher();
  auto cb = CommandBuffer::NewForGraphics(escher, false);
  CommandBufferPipelineState cbps(nullptr);

  auto cb_static_state = VulkanTester::GetStaticState(cb);
  auto cbps_static_state = cbps.static_state();

  const std::vector<bool> kAllBools{true, false};
  const std::vector<vk::CompareOp> kAllVkCompareOps{
      vk::CompareOp::eNever,          vk::CompareOp::eLess,    vk::CompareOp::eEqual,
      vk::CompareOp::eLessOrEqual,    vk::CompareOp::eGreater, vk::CompareOp::eNotEqual,
      vk::CompareOp::eGreaterOrEqual, vk::CompareOp::eAlways};

  const std::vector<vk::BlendFactor> kAllVkBlendFactors{vk::BlendFactor::eZero,
                                                        vk::BlendFactor::eOne,
                                                        vk::BlendFactor::eSrcColor,
                                                        vk::BlendFactor::eOneMinusSrcColor,
                                                        vk::BlendFactor::eDstColor,
                                                        vk::BlendFactor::eOneMinusDstColor,
                                                        vk::BlendFactor::eSrcAlpha,
                                                        vk::BlendFactor::eOneMinusSrcAlpha,
                                                        vk::BlendFactor::eDstAlpha,
                                                        vk::BlendFactor::eOneMinusDstAlpha,
                                                        vk::BlendFactor::eConstantColor,
                                                        vk::BlendFactor::eOneMinusConstantColor,
                                                        vk::BlendFactor::eConstantAlpha,
                                                        vk::BlendFactor::eOneMinusConstantAlpha,
                                                        vk::BlendFactor::eSrcAlphaSaturate,
                                                        vk::BlendFactor::eSrc1Color,
                                                        vk::BlendFactor::eOneMinusSrc1Color,
                                                        vk::BlendFactor::eSrc1Alpha,
                                                        vk::BlendFactor::eOneMinusSrc1Alpha};
  const std::vector<vk::BlendOp> kAllVkBlendOps{vk::BlendOp::eAdd, vk::BlendOp::eSubtract,
                                                vk::BlendOp::eReverseSubtract, vk::BlendOp::eMin,
                                                vk::BlendOp::eMax};

  const std::vector<vk::StencilOp> kAllVkStencilOps{vk::StencilOp::eKeep,
                                                    vk::StencilOp::eZero,
                                                    vk::StencilOp::eReplace,
                                                    vk::StencilOp::eIncrementAndClamp,
                                                    vk::StencilOp::eDecrementAndClamp,
                                                    vk::StencilOp::eInvert,
                                                    vk::StencilOp::eIncrementAndWrap,
                                                    vk::StencilOp::eDecrementAndWrap};

  const std::vector<vk::PrimitiveTopology> kAllVkPrimitiveTopologies{
      vk::PrimitiveTopology::ePointList,
      vk::PrimitiveTopology::eLineList,
      vk::PrimitiveTopology::eLineStrip,
      vk::PrimitiveTopology::eTriangleList,
      vk::PrimitiveTopology::eTriangleStrip,
      vk::PrimitiveTopology::eTriangleFan,
      vk::PrimitiveTopology::eLineListWithAdjacency,
      vk::PrimitiveTopology::eLineStripWithAdjacency,
      vk::PrimitiveTopology::eTriangleListWithAdjacency,
      vk::PrimitiveTopology::eTriangleStripWithAdjacency,
      vk::PrimitiveTopology::ePatchList};

  const std::vector<vk::CullModeFlags> kAllVkCullModeFlags{
      vk::CullModeFlagBits::eNone, vk::CullModeFlagBits::eFront, vk::CullModeFlagBits::eBack,
      vk::CullModeFlagBits::eFrontAndBack};

  const std::vector<vk::FrontFace> kAllVkFrontFaces{vk::FrontFace::eCounterClockwise,
                                                    vk::FrontFace::eClockwise};

// Helper for StaticStateSetting test, below.  Compares setting state via 1-arg functions on both
// CommandBuffer and CommandBufferPipelineState, and verifies that both have the same effect. Also
// verifies that CommandBuffer dirty bits are set properly.
#define TEST_SETTER(SETTER_NAME, GETTER_NAME, VALUE_LIST)                \
  do {                                                                   \
    bool first = true;                                                   \
    for (const auto& val : VALUE_LIST) {                                 \
      cb->SETTER_NAME(val);                                              \
      cbps.SETTER_NAME(val);                                             \
      EXPECT_EQ(cb_static_state->GETTER_NAME(), val);                    \
      EXPECT_EQ(cbps_static_state->GETTER_NAME(), val);                  \
      EXPECT_TRUE(*cb_static_state == *cbps_static_state);               \
      if (!first) {                                                      \
        EXPECT_EQ(GetAndClearDirty(cb, DirtyBits::kDirtyStaticStateBit), \
                  DirtyBits::kDirtyStaticStateBit);                      \
        EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyStaticStateBit), 0u);    \
      }                                                                  \
      first = false;                                                     \
    }                                                                    \
  } while (0)

// Helper for StaticStateSetting test, below.  Compares setting state via 2-arg functions on both
// CommandBuffer and CommandBufferPipelineState, and verifies that both have the same effect.  Also
// verifies that CommandBuffer dirty bits are set properly.
#define TEST_SETTER2(SETTER_NAME, GETTER_NAME1, GETTER_NAME2, VALUE_LIST1, VALUE_LIST2) \
  do {                                                                                  \
    bool first = true;                                                                  \
    for (const auto& val1 : VALUE_LIST1) {                                              \
      for (const auto& val2 : VALUE_LIST2) {                                            \
        cb->SETTER_NAME(val1, val2);                                                    \
        cbps.SETTER_NAME(val1, val2);                                                   \
        EXPECT_EQ(cb_static_state->GETTER_NAME1(), val1);                               \
        EXPECT_EQ(cbps_static_state->GETTER_NAME1(), val1);                             \
        EXPECT_EQ(cb_static_state->GETTER_NAME2(), val2);                               \
        EXPECT_EQ(cbps_static_state->GETTER_NAME2(), val2);                             \
        if (!first) {                                                                   \
          EXPECT_EQ(GetAndClearDirty(cb, DirtyBits::kDirtyStaticStateBit),              \
                    DirtyBits::kDirtyStaticStateBit);                                   \
          EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyStaticStateBit), 0u);                 \
        }                                                                               \
        first = false;                                                                  \
      }                                                                                 \
    }                                                                                   \
  } while (0)

// Helper for StaticStateSetting test, below.  Compares setting state via 3-arg functions on both
// CommandBuffer and CommandBufferPipelineState, and verifies that both have the same effect.  Also
// verifies that CommandBuffer dirty bits are set properly.
#define TEST_SETTER3(SETTER_NAME, GETTER_NAME1, GETTER_NAME2, GETTER_NAME3, VALUE_LIST1, \
                     VALUE_LIST2, VALUE_LIST3)                                           \
  do {                                                                                   \
    bool first = true;                                                                   \
    for (const auto& val1 : VALUE_LIST1) {                                               \
      for (const auto& val2 : VALUE_LIST2) {                                             \
        for (const auto& val3 : VALUE_LIST3) {                                           \
          cb->SETTER_NAME(val1, val2, val3);                                             \
          cbps.SETTER_NAME(val1, val2, val3);                                            \
          EXPECT_EQ(cb_static_state->GETTER_NAME1(), val1);                              \
          EXPECT_EQ(cbps_static_state->GETTER_NAME1(), val1);                            \
          EXPECT_EQ(cb_static_state->GETTER_NAME2(), val2);                              \
          EXPECT_EQ(cbps_static_state->GETTER_NAME2(), val2);                            \
          EXPECT_EQ(cb_static_state->GETTER_NAME3(), val3);                              \
          EXPECT_EQ(cbps_static_state->GETTER_NAME3(), val3);                            \
          EXPECT_EQ(*cb_static_state, *cbps_static_state);                               \
          if (!first) {                                                                  \
            EXPECT_EQ(GetAndClearDirty(cb, DirtyBits::kDirtyStaticStateBit),             \
                      DirtyBits::kDirtyStaticStateBit);                                  \
            EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyStaticStateBit), 0u);                \
          }                                                                              \
          first = false;                                                                 \
        }                                                                                \
      }                                                                                  \
    }                                                                                    \
  } while (0)

// Helper for StaticStateSetting test, below.  Compares setting state via 4-arg functions on both
// CommandBuffer and CommandBufferPipelineState, and verifies that both have the same effect.  Also
// verifies that CommandBuffer dirty bits are set properly.
#define TEST_SETTER4(SETTER_NAME, GETTER_NAME1, GETTER_NAME2, GETTER_NAME3, GETTER_NAME4, \
                     VALUE_LIST1, VALUE_LIST2, VALUE_LIST3, VALUE_LIST4)                  \
  do {                                                                                    \
    bool first = true;                                                                    \
    for (const auto& val1 : VALUE_LIST1) {                                                \
      for (const auto& val2 : VALUE_LIST2) {                                              \
        for (const auto& val3 : VALUE_LIST3) {                                            \
          for (const auto& val4 : VALUE_LIST4) {                                          \
            cb->SETTER_NAME(val1, val2, val3, val4);                                      \
            cbps.SETTER_NAME(val1, val2, val3, val4);                                     \
            EXPECT_EQ(cb_static_state->GETTER_NAME1(), val1);                             \
            EXPECT_EQ(cbps_static_state->GETTER_NAME1(), val1);                           \
            EXPECT_EQ(cb_static_state->GETTER_NAME2(), val2);                             \
            EXPECT_EQ(cbps_static_state->GETTER_NAME2(), val2);                           \
            EXPECT_EQ(cb_static_state->GETTER_NAME3(), val3);                             \
            EXPECT_EQ(cbps_static_state->GETTER_NAME3(), val3);                           \
            EXPECT_EQ(cb_static_state->GETTER_NAME4(), val4);                             \
            EXPECT_EQ(cbps_static_state->GETTER_NAME4(), val4);                           \
            EXPECT_EQ(*cb_static_state, *cbps_static_state);                              \
            if (!first) {                                                                 \
              EXPECT_EQ(GetAndClearDirty(cb, DirtyBits::kDirtyStaticStateBit),            \
                        DirtyBits::kDirtyStaticStateBit);                                 \
              EXPECT_EQ(GetDirty(cb, DirtyBits::kDirtyStaticStateBit), 0u);               \
            }                                                                             \
            first = false;                                                                \
          }                                                                               \
        }                                                                                 \
      }                                                                                   \
    }                                                                                     \
  } while (0)

  TEST_SETTER2(SetDepthTestAndWrite, get_depth_test, get_depth_write, kAllBools, kAllBools);

  TEST_SETTER(SetWireframe, get_wireframe, kAllBools);

  TEST_SETTER(SetDepthCompareOp, get_depth_compare, kAllVkCompareOps);

  TEST_SETTER(SetBlendEnable, get_blend_enable, kAllBools);

  TEST_SETTER4(SetBlendFactors, get_src_color_blend, get_src_alpha_blend, get_dst_color_blend,
               get_dst_alpha_blend, kAllVkBlendFactors, kAllVkBlendFactors, kAllVkBlendFactors,
               kAllVkBlendFactors);

  TEST_SETTER2(SetBlendOp, get_color_blend_op, get_alpha_blend_op, kAllVkBlendOps, kAllVkBlendOps);

  TEST_SETTER(SetColorWriteMask, get_color_write_mask,
              std::vector<uint32_t>({0x0000000, 0x00000001}));

  TEST_SETTER(SetDepthBias, get_depth_bias_enable, kAllBools);
  TEST_SETTER(SetStencilTest, get_stencil_test, kAllBools);
  TEST_SETTER4(SetStencilFrontOps, get_stencil_front_compare_op, get_stencil_front_pass,
               get_stencil_front_fail, get_stencil_front_depth_fail, kAllVkCompareOps,
               kAllVkStencilOps, kAllVkStencilOps, kAllVkStencilOps);
  TEST_SETTER4(SetStencilBackOps, get_stencil_back_compare_op, get_stencil_back_pass,
               get_stencil_back_fail, get_stencil_back_depth_fail, kAllVkCompareOps,
               kAllVkStencilOps, kAllVkStencilOps, kAllVkStencilOps);

  TEST_SETTER(SetPrimitiveTopology, get_primitive_topology, kAllVkPrimitiveTopologies);

  TEST_SETTER(SetPrimitiveRestart, get_primitive_restart, kAllBools);

  TEST_SETTER3(SetMultisampleState, get_alpha_to_coverage, get_alpha_to_one, get_sample_shading,
               kAllBools, kAllBools, kAllBools);

  TEST_SETTER(SetFrontFace, get_front_face, kAllVkFrontFaces);

  TEST_SETTER(SetCullMode, get_cull_mode, kAllVkCullModeFlags);

#undef TEST_SETTER
#undef TEST_SETTER2
#undef TEST_SETTER3
#undef TEST_SETTER4

  // TODO(fxbug.dev/7174): ideally only submitted CommandBuffers would need to be cleaned
  // up: if a never-submitted CB is destroyed, then it shouldn't keep anything
  // alive, and it shouldn't cause problems in e.g. CommandBufferPool due to a
  // forever-straggling buffer.
  EXPECT_TRUE(cb->Submit(nullptr));
}

}  // namespace
