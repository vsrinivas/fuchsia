// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CAMERA_LIB_RAW_FORMATS_RAW10_H_
#define SRC_CAMERA_LIB_RAW_FORMATS_RAW10_H_

#include "src/camera/lib/raw_formats/raw.h"

namespace camera::raw {
namespace raw10_internal {

/* This file contains the format descriptors for the RAW10 formats defined by Android. The RAW10
   definition only covers the packing of samples in sequence in the buffer, it does not specify
   any specific bayer phase or set of bayer phases. Thus the descriptors in this file combine the
   RAW10 packing rules with the 4 common bayer phases. More can be added if necessary.

   The RAW10 packing rules also match the packing layout commonly used by many MIPI-CSI busses when
   writing to a memory buffer.
*/

// A block of 4 pixels in 5 bytes, repeated enough time to fill the width of the image (which must
// be a multiple of 4 pixels). The 8 most significant bits of each pixel get their own byte,
// followed by a byte that contains the 2 least significant bits of each pixel in reverse order.
// clang-format off
//                   index,       mask, shift.
constexpr PixelPiece p00(0, 0b11111111,  2);
constexpr PixelPiece p10(1, 0b11111111,  2);
constexpr PixelPiece p20(2, 0b11111111,  2);
constexpr PixelPiece p30(3, 0b11111111,  2);
constexpr PixelPiece p31(3, 0b11000000, -6);
constexpr PixelPiece p21(2, 0b00110000, -4);
constexpr PixelPiece p11(1, 0b00001100, -2);
constexpr PixelPiece p01(0, 0b00000011,  0);
// clang-format on
constexpr const Chunk* pixels[] = {&p00, &p10, &p20, &p30, &p31, &p21, &p11, &p01};
constexpr PointerList<Chunk> pixel_list(&(pixels[0]), /*size*/ 8);
constexpr PackingBlock pixel_block(pixel_list, ChunkRepeat::fill_width());

// If the image row_stride is greater than with * (5 / 4), padding will be added to make up the
// difference.
constexpr Padding stride_pad(0, ChunkRepeat::fill_stride());
constexpr const Chunk* line_block_chunks[] = {&pixel_block, &stride_pad};
constexpr PointerList<Chunk> line_block_chunk_list(&(line_block_chunks[0]), /*size*/ 2);

}  // namespace raw10_internal

// This packing block represents a line of pixels (potentially with padding at the end) which
// repeats as necessary (height times) to fill the image. The height (in pixels) must be even.
constexpr PackingBlock kRaw10PackingBlock(raw10_internal::line_block_chunk_list,
                                          ChunkRepeat::fill_image());

constexpr RawFormat kRaw10FormatBGGR(kRaw10PackingBlock, kBayerBGGR, kBayer10DepthMap);
constexpr RawFormat kRaw10FormatGBRG(kRaw10PackingBlock, kBayerGBRG, kBayer10DepthMap);
constexpr RawFormat kRaw10FormatGRBG(kRaw10PackingBlock, kBayerGRBG, kBayer10DepthMap);
constexpr RawFormat kRaw10FormatRGGB(kRaw10PackingBlock, kBayerRGGB, kBayer10DepthMap);

}  // namespace camera::raw

#endif  // SRC_CAMERA_LIB_RAW_FORMATS_RAW10_H_
