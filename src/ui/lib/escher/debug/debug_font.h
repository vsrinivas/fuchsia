// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_DEBUG_DEBUG_FONT_H_
#define SRC_UI_LIB_ESCHER_DEBUG_DEBUG_FONT_H_

#include <memory>
#include <vulkan/vulkan.hpp>

#include "src/ui/lib/escher/forward_declarations.h"

namespace escher {

// This is a monospaced bitmap font renderer that blits to the output image for
// maximal performance.
class DebugFont {
 public:
  static std::unique_ptr<DebugFont> New(BatchGpuUploader* uploader,
                                        ImageFactory* factory);

  // Return RGBA pixels containing a monospace bitmap ASCII font.  Each glyph is
  // 7x7 pixels (including 1 pixel of padding around each edge of the glyph).
  // The glyphs are packed in a single column with total dimensions 7x1792.
  static std::unique_ptr<uint8_t[]> GetFontPixels();

  static constexpr uint32_t kGlyphWidth = 7;
  static constexpr uint32_t kGlyphHeight = 7;
  static constexpr uint32_t kGlyphPadding = 1;
  static constexpr uint32_t kNumGlyphs = 256;

  // Blit the specified text into |output_image|.  |target_offset| is the
  // top-left corner of the display region.  |scale| is an integer multiplier
  // that scales the width and height of each displayed glyph.  The caller is
  // responsible for setting memory barriers; |output_image| must have layout
  // vk::ImageLayout::eTransferDstOptimal before Blit() is called.
  void Blit(CommandBuffer* cb, const std::string& text,
            const ImagePtr& output_image, vk::Offset2D target_offset,
            int32_t scale);

 private:
  explicit DebugFont(ImagePtr image);
  ImagePtr image_;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_DEBUG_DEBUG_FONT_H_
