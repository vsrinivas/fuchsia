// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/camera/lib/raw_formats/raw_lookups.h"

#include <zircon/assert.h>

#include <unordered_set>

namespace camera::raw {
namespace {

// This recursive function goes and finds all the PixelPieces for the given pixel index as well as
// the offsets into the buffer of the bytes they describe bits within.
using PixelPieceOffsetList = std::vector<std::tuple<uint64_t, const PixelPiece&>>;
void find_offsets(const PackingBlock& block, uint32_t pixel_index, uint64_t initial_offset,
                  PixelPieceOffsetList& out) {
  int32_t pixel_sum = 0;
  uint64_t bits_offset = 0;
  std::unordered_set<uint32_t> seen_pixels;

  for (uint64_t i = 0; i < block.chunks().size(); ++i) {
    const auto& chunk = block.chunks()[i];

    if (chunk->type() == ChunkType::PIXEL_PIECE) {
      const PixelPiece& piece = Chunk::get<PixelPiece>(chunk);

      if (!seen_pixels.contains(piece.pixel_index())) {
        ++pixel_sum;
        seen_pixels.insert(piece.pixel_index());
      }

      // Is this a piece of our pixel?
      if (piece.pixel_index() == pixel_index) {
        // The offset of this pixel into the buffer is going to be the offset in (whole) bytes
        // into this block, plus the offset in bytes of the block itself (initial_offset).
        uint64_t offset = initial_offset + (bits_offset / 8);
        out.emplace_back(offset, piece);
      }
      bits_offset += piece.num_bits();

    } else if (chunk->type() == ChunkType::PADDING) {
      // Just some padding, move along.
      const Padding& pad = Chunk::get<Padding>(chunk);
      ZX_ASSERT_MSG(pad.repeat().type == ChunkRepeatType::FINITE,
                    "Non FINITE repeat chunk type found in PackingBlock.");
      bits_offset += pad.num_bits();

    } else if (chunk->type() == ChunkType::PACKING_BLOCK) {
      const PackingBlock& sub_block = Chunk::get<PackingBlock>(chunk);
      ZX_ASSERT_MSG(sub_block.repeat().type == ChunkRepeatType::FINITE,
                    "Non FINITE repeat chunk type found in PackingBlock.");
      ZX_ASSERT_MSG(bits_offset % 8 == 0, "Saw sub block that isn't byte aligned.");

      // First check if the pixel we're looking for is anywhere within this block/its repeats.
      // If not, increment the pixel and bit sums and move on.
      uint32_t num_pixels = sub_block.repeat().times * sub_block.num_pixels();
      if (pixel_index >= num_pixels) {
        pixel_sum += num_pixels;
        bits_offset += sub_block.repeat().times * sub_block.num_bits();
        continue;
      }

      // Ok, the pixel is somewhere within the repeats of this block. Figure out which one and
      // prepare to recurse. The block index is also equal to the number of blocks which come
      /// before the block we're about to recurse on.
      uint32_t block_index = pixel_index / sub_block.num_pixels();

      // We add the number of pixels in all the blocks before the block we will recurse on to the
      // pixel_sum before we use it to calculate the pixel index in the sub-block.
      pixel_sum += block_index * sub_block.num_pixels();
      // The pixel index in the sub block will be the index in this block minus the number of
      // pixels that came before it in this block.
      uint32_t sub_pixel_index = pixel_index - pixel_sum;

      // Update bits_offset with the size of all the blocks before the block we will recurse on.
      bits_offset += block_index * sub_block.num_bits();
      // We pass the offset to the sub block as the initial offset in the recursive call.
      uint64_t sub_block_offset = initial_offset + (bits_offset / 8);

      // Make the recursive call and return.
      find_offsets(sub_block, sub_pixel_index, sub_block_offset, out);
      return;
    }
  }
}

}  // namespace

uint64_t GetPixel(const RawFormatInstance& format_instance, uint32_t pixel_index,
                  const uint8_t* buffer, size_t buffer_size) {
  ZX_ASSERT_MSG(buffer, "GetPixel given null buffer.");
  ZX_ASSERT_MSG(pixel_index < (format_instance.width * format_instance.height),
                "pixel_index out of bounds.");
  const PackingBlock& packing = format_instance.packing_block;

  // Figure out which of the top level block repetitions our pixel must be in and call find_offsets.
  uint32_t block_index = pixel_index / packing.num_pixels();
  uint32_t sub_pixel_index = pixel_index - (block_index * packing.num_pixels());
  uint64_t sub_block_offset = (block_index * packing.num_bits()) / 8;

  PixelPieceOffsetList offsets;
  find_offsets(packing, sub_pixel_index, sub_block_offset, offsets);

  uint64_t out = 0;
  for (const auto& [offset, piece] : offsets) {
    ZX_ASSERT_MSG(offset < buffer_size, "Pixel offset calculated to be greater than buffer size.");

    uint64_t piece_data;
    int8_t shift = piece.shift();
    if (shift >= 0) {
      piece_data = (buffer[offset] & piece.mask()) << shift;
    } else {
      piece_data = (buffer[offset] & piece.mask()) >> -shift;
    }
    out |= piece_data;
  }

  return out;
}

void SetPixel(const RawFormatInstance& format_instance, uint32_t pixel_index, uint64_t pixel_value,
              uint8_t* buffer, size_t buffer_size) {
  ZX_ASSERT_MSG(buffer, "SetPixel given null buffer.");
  ZX_ASSERT_MSG(pixel_index < (format_instance.width * format_instance.height),
                "pixel_index out of bounds.");
  const PackingBlock& packing = format_instance.packing_block;

  // Figure out which of the top level block repetitions our pixel must be in and call find_offsets.
  uint32_t block_index = pixel_index / packing.num_pixels();
  uint32_t sub_pixel_index = pixel_index - (block_index * packing.num_pixels());
  uint64_t sub_block_offset = (block_index * packing.num_bits()) / 8;

  PixelPieceOffsetList offsets;
  find_offsets(packing, sub_pixel_index, sub_block_offset, offsets);

  for (const auto& [offset, piece] : offsets) {
    ZX_ASSERT_MSG(offset < buffer_size, "Pixel offset calculated to be greater than buffer size.");
    uint8_t byte_value = buffer[offset];
    uint64_t mask = piece.mask();
    int8_t shift = piece.shift();

    // Clear the part of the byte we're about to overwrite.
    byte_value &= ~mask;
    // Get the part of the new pixel value we're going to OR in.
    uint64_t new_byte_segment;
    if (shift >= 0) {
      mask <<= shift;
      new_byte_segment = (pixel_value & mask) >> shift;
    } else {
      shift = -shift;
      mask >>= shift;
      new_byte_segment = (pixel_value & mask) << shift;
    }
    byte_value |= static_cast<uint8_t>(new_byte_segment);
    buffer[offset] = byte_value;
  }
}

}  // namespace camera::raw
