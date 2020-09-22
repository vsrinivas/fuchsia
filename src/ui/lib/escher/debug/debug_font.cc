// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/debug/debug_font.h"

#include <lib/syslog/cpp/macros.h>

#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/types/color.h"
#include "src/ui/lib/escher/util/alloca.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/image.h"

namespace escher {

std::unique_ptr<DebugFont> DebugFont::New(BatchGpuUploader* uploader, ImageFactory* factory) {
  auto bytes = DebugFont::GetFontPixels();

  auto image = image_utils::NewRgbaImage(factory, uploader, kGlyphWidth, kGlyphHeight * kNumGlyphs,
                                         bytes.get(), vk::ImageLayout::eTransferSrcOptimal);
  return std::unique_ptr<DebugFont>(new DebugFont(std::move(image)));
}

DebugFont::DebugFont(ImagePtr image) : image_(std::move(image)) { FX_DCHECK(image_); }

void DebugFont::Blit(CommandBuffer* cb, const std::string& text, const ImagePtr& target,
                     vk::Offset2D offset, int32_t scale) {
  cb->KeepAlive(target);

  vk::ImageBlit* regions = ESCHER_ALLOCA(vk::ImageBlit, text.length());

  const int32_t dst_top = offset.y;
  const int32_t dst_bottom = offset.y + kGlyphHeight * scale;

  uint32_t region_count = 0;
  for (size_t i = 0; i < text.length(); ++i) {
    const uint32_t glyph_index = static_cast<uint32_t>(text[i]);
    if (glyph_index >= kNumGlyphs) {
      continue;
    }

    const int32_t src_top = glyph_index * kGlyphHeight;
    const int32_t src_bottom = src_top + kGlyphHeight;
    const int32_t dst_left = offset.x + region_count * kGlyphWidth * scale;
    const int32_t dst_right = dst_left + kGlyphWidth * scale;

    auto& region = regions[region_count];
    region.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.srcSubresource.mipLevel = 0;
    region.srcSubresource.baseArrayLayer = 0;
    region.srcSubresource.layerCount = 1;
    region.dstSubresource = region.srcSubresource;
    region.srcOffsets[0] = vk::Offset3D(0, src_top, 0);
    region.srcOffsets[1] = vk::Offset3D(kGlyphWidth, src_bottom, 1);
    region.dstOffsets[0] = vk::Offset3D(dst_left, dst_top, 0);
    region.dstOffsets[1] = vk::Offset3D(dst_right, dst_bottom, 1);

    ++region_count;
  }

  if (region_count > 0) {
    cb->vk().blitImage(image_->vk(), vk::ImageLayout::eTransferSrcOptimal, target->vk(),
                       vk::ImageLayout::eTransferDstOptimal, region_count, regions,
                       vk::Filter::eNearest);
  }
}

std::unique_ptr<uint8_t[]> DebugFont::GetFontPixels() {
  // Fill all glyph_bits with placeholder; we will replace some of these for the
  // glyphs that we actually have bit patterns for.
  uint32_t glyph_bits[kNumGlyphs];
  for (size_t i = 0; i < kNumGlyphs; ++i) {
    // placeholder (black square)
    // 11111
    // 11111
    // 11111
    // 11111
    // 11111
    // == 11111 11111 11111 11111 11111
    // == 1 1111 1111 1111 1111 1111 1111
    // == 0x1ffffff
    glyph_bits[i] = 0x1ffffff;
  }

  // TODO(fxbug.dev/7297): glyphs for ASCII 0x0 - 0x1F

  // 'space'
  // .....
  // .....
  // .....
  // .....
  // .....
  glyph_bits[32] = 0x0;

  // '!'
  // ..1..
  // ..1..
  // ..1..
  // .....
  // ..1..
  // == 00100 00100 00100 00000 00100
  // == 0100 0010 0001 0000 0000 0100
  glyph_bits[int32_t{'!'}] = 0x421004;

  // '"'
  // .1.1.
  // .1.1.
  // .....
  // .....
  // .....
  // == 01010 01010 00000 00000 00000
  // == 1010 0101 0000 0000 0000 0000
  glyph_bits[int32_t{'"'}] = 0xa50000;

  // '#'
  // .1.1.
  // 11111
  // .1.1.
  // 11111
  // .1.1.
  // == 01010 11111 01010 11111 01010
  // == 1010 1111 1010 1011 1110 1010
  glyph_bits[int32_t{'#'}] = 0xafabea;

  // TODO(fxbug.dev/7297): glyphs for $%&\()*+

  // '-'
  // .....
  // .....
  // 11111
  // .....
  // .....
  // == 00000 00000 11111 00000 00000
  // == 0111 1100 0000 0000
  glyph_bits[int32_t{'-'}] = 0x7c00;

  // '.'
  // .....
  // .....
  // .....
  // .....
  // ..1..
  // == 00000 00000 00000 00000 00100
  glyph_bits[int32_t{'.'}] = 0x4;

  // '0'
  // 11111
  // 1...1
  // 1...1
  // 1...1
  // 11111
  // == 11111 10001 10001 10001 11111
  // == 0001 1111 1000 1100 0110 0011 1111
  glyph_bits[int32_t{'0'}] = 0x1f8c63f;

  // '1'
  // ..1..
  // ..1..
  // ..1..
  // ..1..
  // ..1..
  // == 00100 00100 00100 00100 00100
  // == 0100 0010 0001 0000 1000 0100
  glyph_bits[int32_t{'1'}] = 0x421084;

  // '2'
  // 11111
  // ....1
  // 11111
  // 1....
  // 11111
  // == 11111 00001 11111 10000 11111
  // == 0011 1111 0000 1111 1110 0001 1111
  glyph_bits[int32_t{'2'}] = 0x3f0fe1f;

  // '3'
  // 11111
  // ....1
  // ..111
  // ....1
  // 11111
  // == 11111 00001 00111 00001 11111
  // == 0001 1111 0000 1001 1100 0011 1111
  glyph_bits[int32_t{'3'}] = 0x1f09c3f;

  // '4'
  // 1...1
  // 1...1
  // 11111
  // ....1
  // ....1
  // == 10001 10001 11111 00001 00001
  // == 0001 0001 1000 1111 1100 0010 0001
  glyph_bits[int32_t{'4'}] = 0x118fc21;

  // '5'
  // 11111
  // 1....
  // 11111
  // ....1
  // 11111
  // == 11111 10000 11111 00001 11111
  // == 0001 1111 1000 0111 1100 0011 1111
  glyph_bits[int32_t{'5'}] = 0x1f87c3f;

  // '6'
  // 11111
  // 1....
  // 11111
  // 1...1
  // 11111
  // == 11111 10000 11111 10001 11111
  // == 0001 1111 1000 0111 1110 0011 1111
  glyph_bits[int32_t{'6'}] = 0x1f87e3f;

  // '7'
  // 11111
  // ....1
  // ....1
  // ....1
  // ....1
  // == 11111 00001 00001 00001 00001
  // == 0001 1111 0000 1000 0100 0010 0001
  glyph_bits[int32_t{'7'}] = 0x1f08421;

  // '8'
  // 11111
  // 1...1
  // 11111
  // 1...1
  // 11111
  // == 11111 10001 11111 10001 11111
  // == 0001 1111 1000 1111 1110 0011 1111
  glyph_bits[int32_t{'8'}] = 0x1f8fe3f;

  // '9'
  // 11111
  // 1...1
  // 11111
  // ....1
  // 11111
  // == 11111 10001 11111 00001 11111
  // == 0001 1111 1000 1111 1100 0011 1111
  glyph_bits[int32_t{'9'}] = 0x1f8fc3f;

  // TODO(fxbug.dev/7297): glyphs for ASCII 0x3A - 0x40 and 0x44 - 0x7F

  // ‘A’
  // ..1..
  // .1.1.
  // 11111
  // 1...1
  // 1...1
  // == 00100 01010 11111 10001 10001
  glyph_bits[int32_t{'A'}] = 0x457E31;

  // ‘B’
  // 1111.
  // 1...1
  // 1111.
  // 1...1
  // 1111.
  // == 11110 10001 11110 10001 11110
  glyph_bits[int32_t{'B'}] = 0x1E8FA3E;

  // ‘C’
  // .1111
  // 1....
  // 1....
  // 1....
  // .1111
  // == 11111 10000 10000 10000 11111
  glyph_bits[int32_t{'C'}] = 0x1F8421F;

  // ‘D’
  // 1111.
  // 1...1
  // 1...1
  // 1...1
  // 1111.
  // == 11110 10001 10001 10001 11110
  glyph_bits[int32_t{'D'}] = 0x1E8C63E;

  // ‘E’
  // 11111
  // 1....
  // 1111.
  // 1....
  // 11111
  // == 11111 10000 11110 10000 11111
  glyph_bits[int32_t{'E'}] = 0x1F87A1F;

  // ‘F’
  // 11111
  // 1....
  // 1111.
  // 1....
  // 1....
  // == 11111 10000 11110 10000 10000
  glyph_bits[int32_t{'F'}] = 0x1F87A10;

  // TODO(fxbug.dev/7297): glyphs for ASCII 0x47 - 0x48

  // ‘I’
  // 11111
  // ..1..
  // ..1..
  // ..1..
  // 11111
  // == 11111 00100 00100 00100 11111
  glyph_bits[int32_t{'I'}] = 0x1F2109F;

  // TODO(fxbug.dev/7297): glyphs for ASCII 0x4A - 0x4C

  // ‘M’
  // .1.1.
  // 11.11
  // 1.1.1
  // 1.1.1
  // 1...1
  // == 01010 11011 10101 10101 10001
  glyph_bits[int32_t{'M'}] = 0xADD6B1;

  // ‘N’
  // 11..1
  // 11..1
  // 1.1.1
  // 1..11
  // 1..11
  // == 11001 11001 10101 10011 10011
  glyph_bits[int32_t{'N'}] = 0x19CD673;

  // ‘O’
  // .111.
  // 1...1
  // 1...1
  // 1...1
  // .111.
  // == 01110 10001 10001 10001 01110
  glyph_bits[int32_t{'O'}] = 0xE8C62E;

  // // TODO(fxbug.dev/7297): glyphs for ASCII 0x50 - 0x51

  // ‘R'
  // 1111.
  // 1...1
  // 1111.
  // 1.1..
  // 1..11
  // == 11110 10001 11110 10100 10011
  glyph_bits[int32_t{'R'}] = 0x1E8FA93;

  // ‘S’
  // .1111
  // 1....
  // .111.
  // ....1
  // 1111.
  // == 01111 10000 01110 00001 11110
  glyph_bits[int32_t{'S'}] = 0xF8383E;

  // ‘T’
  // 11111
  // ..1..
  // ..1..
  // ..1..
  // ..1..
  // == 11111 00100 00100 00100 00100
  glyph_bits[int32_t{'T'}] = 0x1F21084;

  // TODO(fxbug.dev/7297): glyphs for ASCII 0x55 - 0x7F

  // Process the bits that describe each glyph, turning them into black
  // and white pixels.
  constexpr uint32_t kInnerWidth = kGlyphWidth - 2 * kGlyphPadding;
  constexpr uint32_t kInnerHeight = kGlyphHeight - 2 * kGlyphPadding;
  static_assert(kInnerWidth == 5, "unpadded glyph width must be 5.");
  static_assert(kInnerHeight == 5, "unpadded glyph height must be 5.");

  constexpr uint32_t kBytesPerPixel = 4;
  constexpr uint32_t kBytesPerRow = kGlyphWidth * kBytesPerPixel;
  constexpr uint32_t kBytesPerGlyph = kBytesPerRow * (kGlyphHeight);

  auto output = std::make_unique<uint8_t[]>(kNumGlyphs * kBytesPerGlyph);

  // Avoid endianness problems by always specifying in byte order.
  constexpr ColorRgba kBlack(0, 0, 0, 0xff);
  constexpr ColorRgba kWhite(0xff, 0xff, 0xff, 0xff);
  for (size_t i = 0; i < kNumGlyphs; ++i) {
    ColorRgba* glyph = reinterpret_cast<ColorRgba*>(output.get() + i * kBytesPerGlyph);

    // Fill the entire glyph with white, including the padding.
    // We could just fill the padding, but the performance improvement
    // would be negligible (this is only done once).
    for (size_t x = 0; x < kGlyphWidth; ++x) {
      for (size_t y = 0; y < kGlyphHeight; ++y) {
        glyph[x + y * kGlyphWidth] = kWhite;
      }
    }

    // Fill in the pixels of the glyph itself (not the padding).
    constexpr uint32_t kMaxGlyphShift = kInnerWidth * kInnerHeight - 1;
    for (size_t x = 0; x < kInnerWidth; ++x) {
      for (size_t y = 0; y < kInnerHeight; ++y) {
        size_t shift = kMaxGlyphShift - (y * kInnerWidth + x);
        ColorRgba color = ((glyph_bits[i] >> shift) & 1) ? kBlack : kWhite;
        glyph[(x + kGlyphPadding) + (y + kGlyphPadding) * kGlyphWidth] = color;
      }
    }
  }

  return output;
}

}  // namespace escher
