// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/ui/examples/escher/rainfall/rainfall_demo.h"
#include "src/ui/lib/escher/renderer/frame.h"
#include "src/ui/lib/escher/test/gtest_escher.h"
#include "src/ui/lib/escher/test/test_with_vk_validation_layer.h"
#include "src/ui/lib/escher/util/image_utils.h"

static constexpr uint32_t kFramebufferWidth = 1024;
static constexpr uint32_t kFramebufferHeight = 1024;

using RainfallDemoTest = escher::test::TestWithVkValidationLayer;

VK_TEST_F(RainfallDemoTest, SmokeTest) {
  RainfallDemo demo(escher::test::GetEscher()->GetWeakPtr(), 0, nullptr);
  auto escher = demo.escher();

  auto output_image = escher::image_utils::NewColorAttachmentImage(
      escher->image_cache(), kFramebufferWidth, kFramebufferHeight,
      vk::ImageUsageFlagBits::eTransferDst);
  output_image->set_swapchain_layout(vk::ImageLayout::eColorAttachmentOptimal);

  auto frame = escher->NewFrame("Rainfall SmokeTest", 0, 0);

  frame->cmds()->ImageBarrier(
      output_image, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
      vk::PipelineStageFlagBits::eAllCommands, vk::AccessFlagBits::eColorAttachmentWrite,
      vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::AccessFlagBits::eColorAttachmentWrite);

  demo.DrawFrame(frame, output_image, escher::SemaphorePtr());

  bool frame_done = false;
  frame->EndFrame(escher::SemaphorePtr(), [&]() { frame_done = true; });
  escher->vk_device().waitIdle();
  escher->Cleanup();
  EXPECT_TRUE(frame_done);
}
