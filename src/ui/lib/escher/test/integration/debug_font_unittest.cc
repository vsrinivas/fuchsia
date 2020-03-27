// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/debug/debug_font.h"

#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/test/common/readback_test.h"
#include "src/ui/lib/escher/types/color.h"
#include "src/ui/lib/escher/types/color_histogram.h"
#include "src/ui/lib/escher/util/image_utils.h"

#include <vulkan/vulkan.hpp>

namespace {
using namespace escher;

// Extends ReadbackTest by providing a ready-to-use DebugFont instance.
class DebugFontTest : public ReadbackTest {
 protected:
  // |ReadbackTest|
  void SetUp() override {
    ReadbackTest::SetUp();

    auto uploader = BatchGpuUploader::New(escher());
    ImageFactoryAdapter factory(escher()->gpu_allocator(), escher()->resource_recycler());
    debug_font_ = DebugFont::New(uploader.get(), &factory);
    uploader->Submit();
  }

  // |ReadbackTest|
  void TearDown() override {
    debug_font_.reset();

    ReadbackTest::TearDown();
  }

  DebugFont* debug_font() const { return debug_font_.get(); }

 private:
  std::unique_ptr<DebugFont> debug_font_;
};

VK_TEST_F(DebugFontTest, Glyphs) {
  // Constants relating to individual glyphs.
  constexpr uint32_t kNumPixelsPerGlyph = DebugFont::kGlyphWidth * DebugFont::kGlyphHeight;

  constexpr ColorBgra kBlack(0, 0, 0, 255);
  constexpr ColorBgra kWhite(255, 255, 255, 255);

  for (int32_t scale = 1; scale <= 4; ++scale) {
    const int32_t scale_squared = scale * scale;

    ReadbackTest::FrameData fd = NewFrame(vk::ImageLayout::eTransferDstOptimal);
    auto& frame = fd.frame;

    // |expected_black| is the total number of black pixels *within* the glyphs
    // *before* scaling.  In other words, black background pixels outside of the
    // glyph bounds are not counted.  Also, consider the glyph "!" which has 4
    // black pixels all in one vertical column (3 black, 1 white, 1 black)...
    // if the scale is 2 then both the width and height are doubled so the
    // number of black pixels in the glyph after scaling is 16.
    std::function<void(std::string, size_t)> draw_and_check_histogram = [&](std::string glyphs,
                                                                            size_t expected_black) {
      debug_font()->Blit(frame->cmds(), glyphs, fd.color_attachment, {0, 10 * scale}, scale);

      size_t expected_white =
          (glyphs.length() * kNumPixelsPerGlyph - expected_black) * scale_squared;

      auto bytes = ReadbackFromColorAttachment(frame, vk::ImageLayout::eTransferDstOptimal,
                                               vk::ImageLayout::eTransferDstOptimal);

      const ColorHistogram<ColorBgra> histogram(bytes.data(),
                                                kFramebufferWidth * kFramebufferHeight);

      EXPECT_EQ(2U, histogram.size());
      EXPECT_EQ(histogram[kWhite], expected_white)
          << "FAILED WHILE DRAWING \"" << glyphs << "\" AT SCALE: " << scale;
      EXPECT_EQ(histogram[kBlack], kNumFramebufferPixels - expected_white);
    };

    // Each time, we draw on top of the previous glyph.
    draw_and_check_histogram(" ", 0);
    draw_and_check_histogram("1", 5);
    draw_and_check_histogram("A", 12);
    draw_and_check_histogram("!", 4);

    // Draw a glyph that has not been defined, it should draw a black square.
    draw_and_check_histogram("Z", 25);

    // Draw several glyphs next to each other.
    draw_and_check_histogram(" 1A!", 0 + 5 + 12 + 4);

    frame->EndFrame(SemaphorePtr(), []() {});
  }

  escher()->vk_device().waitIdle();
  ASSERT_TRUE(escher()->Cleanup());
}

}  // namespace
