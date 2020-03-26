// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_TEST_COMMON_READBACK_TEST_H_
#define SRC_UI_LIB_ESCHER_TEST_COMMON_READBACK_TEST_H_

#include <vector>

#include <gtest/gtest.h>

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/test/common/gtest_escher.h"

#include <vulkan/vulkan.hpp>

namespace escher {

// Test fixture for Escher tests that need to read back pixels from the
// framebuffer.
class ReadbackTest : public ::testing::Test {
 protected:
  static constexpr uint32_t kFramebufferWidth = 512;
  static constexpr uint32_t kFramebufferHeight = 512;
  static constexpr uint32_t kNumFramebufferPixels = kFramebufferWidth * kFramebufferHeight;
  static constexpr uint32_t kFramebufferBytesPerPixel = 4;
  static constexpr uint32_t kNumFramebufferBytes =
      kNumFramebufferPixels * kFramebufferBytesPerPixel;
  static constexpr vk::Format kColorFormat = vk::Format::eB8G8R8A8Unorm;
  static constexpr vk::Format kDepthFormat = vk::Format::eD24UnormS8Uint;

  // NewFrame() returns a color and a depth attachment that can be rendered
  // into.  The color attachment is first cleared to black via a blit operation,
  // which is useful for tests that don't use a render pass and therefore can't
  // use a clear command.  Synchronization is provided by a barrier.
  struct FrameData {
    FramePtr frame;
    ImagePtr color_attachment;
    ImagePtr depth_attachment;
  };
  FrameData NewFrame(vk::ImageLayout framebuffer_layout);

  // Submits the frame's commands via SubmitPartialFrame(), after first adding
  // commands to readback the output image.  Waits until the Vulkan device is
  // idle, then memcpys the image bytes into the returned result.
  std::vector<uint8_t> ReadbackFromColorAttachment(const FramePtr& frame,
                                                   vk::ImageLayout current_image_layout,
                                                   vk::ImageLayout final_image_layout);

  const EscherWeakPtr& escher() const { return escher_; }

  // |::testing::Test|
  void SetUp() override;

  // |::testing::Test|
  void TearDown() override;

 private:
  EscherWeakPtr escher_;
  ImagePtr color_attachment_;
  ImagePtr depth_attachment_;
  ImagePtr black_;
  BufferPtr readback_buffer_;
  std::unique_ptr<ReadbackTest::FrameData> frame_data_;
  uint64_t frame_number_ = 0;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_TEST_COMMON_READBACK_TEST_H_
