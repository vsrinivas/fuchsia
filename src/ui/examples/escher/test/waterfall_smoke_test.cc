// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/ui/examples/escher/waterfall/waterfall_demo.h"
#include "src/ui/lib/escher/test/gtest_escher.h"
#include "src/ui/lib/escher/util/image_utils.h"

static constexpr uint32_t kFramebufferWidth = 1024;
static constexpr uint32_t kFramebufferHeight = 1024;

VK_TEST(WaterfallDemo, SmokeTest) {
  WaterfallDemo demo(escher::test::GetEscher()->GetWeakPtr(), 0, nullptr);
  auto escher = demo.escher();

  auto output_image = escher::image_utils::NewColorAttachmentImage(
      escher->image_cache(), kFramebufferWidth, kFramebufferHeight,
      vk::ImageUsageFlagBits::eTransferDst);
  output_image->set_swapchain_layout(vk::ImageLayout::eColorAttachmentOptimal);

  auto frame = escher->NewFrame("Waterfall SmokeTest", 0, 0);

  frame->cmds()->ImageBarrier(
      output_image, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
      vk::PipelineStageFlagBits::eBottomOfPipe, vk::AccessFlagBits::eColorAttachmentWrite,
      vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::AccessFlagBits::eColorAttachmentWrite);

  demo.DrawFrame(frame, output_image, escher::SemaphorePtr());

  bool frame_done = false;
  frame->EndFrame(escher::SemaphorePtr(), [&]() { frame_done = true; });
  escher->vk_device().waitIdle();
  escher->Cleanup();
  EXPECT_TRUE(frame_done);
}

VK_TEST(WaterfallDemo, OffscreenBenchmark) {
  WaterfallDemo demo(escher::test::GetEscher()->GetWeakPtr(), 0, nullptr);
  constexpr size_t kNumFrames = 20;
  Demo::RunOffscreenBenchmark(&demo, kFramebufferWidth, kFramebufferHeight,
                              vk::Format::eB8G8R8A8Unorm, kNumFrames);
}

VK_TEST(WaterfallDemo, KeyPresses) {
  WaterfallDemo demo(escher::test::GetEscher()->GetWeakPtr(), 0, nullptr);
  escher::PaperRenderer* renderer = demo.renderer();

  // "D" means toggle debug visualization.
  {
    const bool debugging = renderer->config().debug;
    demo.HandleKeyPress("D");
    EXPECT_NE(debugging, renderer->config().debug);
    demo.HandleKeyPress("D");
    EXPECT_EQ(debugging, renderer->config().debug);
  }

  // "M" means cycle through multisample sample count.
  {
    // Start by cycling until sample count is 1.
    for (auto sample_count = renderer->config().msaa_sample_count; sample_count != 1;) {
      demo.HandleKeyPress("M");
      auto next_sample_count = renderer->config().msaa_sample_count;
      EXPECT_NE(sample_count, next_sample_count);
      sample_count = next_sample_count;
    }
    {
      ::testing::internal::CaptureStderr();
      demo.HandleKeyPress("M");
      std::string output = ::testing::internal::GetCapturedStderr();
      if (output.find("MSAA sample count (2) is not supported") == std::string::npos) {
        EXPECT_EQ(2, renderer->config().msaa_sample_count);
      }
    }
    {
      ::testing::internal::CaptureStderr();
      demo.HandleKeyPress("M");
      std::string output = ::testing::internal::GetCapturedStderr();
      if (output.find("MSAA sample count (2) is not supported") == std::string::npos) {
        EXPECT_EQ(4, renderer->config().msaa_sample_count);
      }
    }
  }
}
