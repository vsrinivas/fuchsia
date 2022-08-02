// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CAMERA_LIB_RAW_FORMATS_RAW_IPU3_H_
#define SRC_CAMERA_LIB_RAW_FORMATS_RAW_IPU3_H_

#include "src/camera/lib/raw_formats/raw.h"

namespace camera::raw {
namespace ipu3_internal {

/* This file contains format descriptors for the 4 pixel formats supported by the Intel IPU3 ISP as
   input. All 4 pixel formats have the same pixel packing layout, they only differ in bayer phase.
   The 4 formats are:
   - IPU3_SBGGR10
   - IPU3_SGBRG10
   - IPU3_SGRBG10
   - IPU3_SRGGB10

   These formats all use 10 bits per pixel.
*/

// A block of 24 pixels in 30 bytes. There is a 4 pixel (5 byte) pattern that repeats 6 times.
namespace block0 {
// clang-format off
//                   index,       mask, shift.
constexpr PixelPiece p80(0, 0b11111111,  0);
constexpr PixelPiece p61(1, 0b11111100, -2);
constexpr PixelPiece P20(0, 0b00000011,  8);
constexpr PixelPiece p42(2, 0b11110000, -4);
constexpr PixelPiece P41(1, 0b00001111,  6);
constexpr PixelPiece p23(3, 0b11000000, -6);
constexpr PixelPiece P62(2, 0b00111111,  4);
constexpr PixelPiece P83(3, 0b11111111,  2);
// clang-format on
constexpr const Chunk* pixels[] = {&p80, &p61, &P20, &p42, &P41, &p23, &P62, &P83};
constexpr PointerList<Chunk> pixel_list(pixels, /*size*/ 8);
constexpr PackingBlock block(pixel_list, ChunkRepeat::finite(6));
}  // namespace block0

// 1 pixel, 2 byte block to finish off a sequence of 25 pixels in 32 bytes.
namespace block1 {
// clang-format off
//                   index,       mask, shift.
constexpr PixelPiece p80(0, 0b11111111,  0);
constexpr PixelPiece P20(0, 0b00000011,  8);
// clang-format on

constexpr Padding pad(6, ChunkRepeat::finite(1));
constexpr const Chunk* pixels[] = {&p80, &pad, &P20};
constexpr PointerList<Chunk> pixel_list(pixels, /*size*/ 3);
constexpr PackingBlock block(pixel_list, ChunkRepeat::finite(1));
}  // namespace block1

constexpr const Chunk* chunks[] = {&block0::block, &block1::block};
constexpr PointerList<Chunk> chunk_list(chunks, /*size*/ 2);

}  // namespace ipu3_internal

// A 25 pixel, 32 byte block that repeats as necessary to fill the image.
constexpr PackingBlock kIpu3PackingBlock(ipu3_internal::chunk_list, ChunkRepeat::fill_image());

constexpr RawFormat kIpu3FormatBGGR10(kIpu3PackingBlock, kBayerBGGR, kBayer10DepthMap);
constexpr RawFormat kIpu3FormatGBRG10(kIpu3PackingBlock, kBayerGBRG, kBayer10DepthMap);
constexpr RawFormat kIpu3FormatGRBG10(kIpu3PackingBlock, kBayerGRBG, kBayer10DepthMap);
constexpr RawFormat kIpu3FormatRGGB10(kIpu3PackingBlock, kBayerRGGB, kBayer10DepthMap);

}  // namespace camera::raw

#endif  // SRC_CAMERA_LIB_RAW_FORMATS_RAW_IPU3_H_
