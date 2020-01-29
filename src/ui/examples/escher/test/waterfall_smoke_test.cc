// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/ui/examples/escher/waterfall/waterfall_demo.h"
#include "src/ui/lib/escher/renderer/frame.h"
#include "src/ui/lib/escher/test/gtest_escher.h"
#include "src/ui/lib/escher/test/test_with_vk_validation_layer.h"
#include "src/ui/lib/escher/util/image_utils.h"

static constexpr uint32_t kFramebufferWidth = 1024;
static constexpr uint32_t kFramebufferHeight = 1024;

using WaterfallDemoTest = escher::test::TestWithVkValidationLayer;

VK_TEST_F(WaterfallDemoTest, SmokeTest) {
  WaterfallDemo demo(escher::test::GetEscher()->GetWeakPtr(), 0, nullptr);
  auto escher = demo.escher();

  auto output_image = escher::image_utils::NewColorAttachmentImage(
      escher->image_cache(), kFramebufferWidth, kFramebufferHeight,
      vk::ImageUsageFlagBits::eTransferDst);
  output_image->set_swapchain_layout(vk::ImageLayout::eColorAttachmentOptimal);

  auto frame = escher->NewFrame("Waterfall SmokeTest", 0, 0);

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

VK_TEST_F(WaterfallDemoTest, OffscreenBenchmark) {
  WaterfallDemo demo(escher::test::GetEscher()->GetWeakPtr(), 0, nullptr);
  constexpr size_t kNumFrames = 20;
  Demo::RunOffscreenBenchmark(&demo, kFramebufferWidth, kFramebufferHeight,
                              vk::Format::eB8G8R8A8Unorm, kNumFrames);
}

VK_TEST_F(WaterfallDemoTest, KeyPresses) {
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
    const std::set<uint8_t> expected_sample_counts(demo.allowed_sample_counts().begin(),
                                                   demo.allowed_sample_counts().end());
    EXPECT_EQ(expected_sample_counts.size(), demo.allowed_sample_counts().size());
    EXPECT_FALSE(expected_sample_counts.empty());

    std::set<uint8_t> observed_sample_counts;
    for (size_t i = 0; i < expected_sample_counts.size(); ++i) {
      demo.HandleKeyPress("M");
      observed_sample_counts.insert(renderer->config().msaa_sample_count);
    }
    EXPECT_EQ(expected_sample_counts, observed_sample_counts);
  }
}
