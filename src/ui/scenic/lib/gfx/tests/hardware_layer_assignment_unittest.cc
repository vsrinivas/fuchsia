// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/hardware_layer_assignment.h"

#include "lib/gtest/test_loop_fixture.h"
#include "src/ui/scenic/lib/gfx/swapchain/swapchain.h"

namespace scenic_impl {
namespace gfx {
namespace test {

using HLATest = ::gtest::TestLoopFixture;

// No-op swapchain for unittest.
class FakeSwapchain : public Swapchain {
 public:
  ~FakeSwapchain() override {}
  // Fake DrawAndPresentFrame always returns draw as successful.
  bool DrawAndPresentFrame(fxl::WeakPtr<scheduling::FrameTimings> frame, size_t swapchain_index,
                           const HardwareLayerAssignment& hla,
                           DrawCallback draw_callback) override {
    return true;
  }

  // |Swapchain|
  bool SetDisplayColorConversion(const ColorTransform& transform) override {
    // Do nothing.
    return true;
  }
  void SetUseProtectedMemory(bool use_protected_memory) override {
    // Do nothing.
  }
  vk::Format GetImageFormat() override { return vk::Format::eUndefined; }
};

TEST_F(HLATest, HasHardwareLayerAssignment) {
  FakeSwapchain fake_swapchain;
  HardwareLayerAssignment hla{
      .items = {{
          .hardware_layer_id = 0,
          .layers = {nullptr},
      }},
      .swapchain = &fake_swapchain,
  };

  EXPECT_TRUE(hla.IsValid());
}

TEST_F(HLATest, HardwareLayerAssignmentMissingSwapchain) {
  HardwareLayerAssignment hla{
      .items = {{
          .hardware_layer_id = 0,
          .layers = {nullptr},
      }},
      .swapchain = nullptr,
  };
  EXPECT_FALSE(hla.IsValid());
}

TEST_F(HLATest, HardwareLayerAssignmentMissingLayers) {
  FakeSwapchain fake_swapchain;
  HardwareLayerAssignment hla{
      .items = {{
          .hardware_layer_id = 0,
      }},
      .swapchain = &fake_swapchain,
  };
  EXPECT_FALSE(hla.IsValid());
}

TEST_F(HLATest, HardwareLayerAssignmentMissingItems) {
  FakeSwapchain fake_swapchain;
  HardwareLayerAssignment hla;
  hla.swapchain = &fake_swapchain;

  EXPECT_FALSE(hla.IsValid());
}

TEST_F(HLATest, HardwareLayerAssignmentDuplicateLayerIDs) {
  FakeSwapchain fake_swapchain;
  HardwareLayerAssignment hla{
      .items = {{
                    .hardware_layer_id = 0,
                    .layers = {nullptr},
                },
                {
                    .hardware_layer_id = 0,
                    .layers = {nullptr},
                }},
      .swapchain = &fake_swapchain,
  };
  EXPECT_FALSE(hla.IsValid());
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
