// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/camera/lib/raw_formats/raw.h"

#include <zircon/assert.h>

#include <unordered_set>

namespace camera::raw {

RawFormatInstance CreateFormatInstance(const RawFormat& format, uint32_t width, uint32_t height,
                                       std::optional<uint32_t> row_stride) {
  ZX_ASSERT_MSG(format.packing_block.repeat().type == ChunkRepeatType::FILL_IMAGE,
                "Top level PackingBlock repeat type must be FILL_IMAGE.");

  std::function<PackingBlock(const PackingBlock&)> create_instance_packing;
  create_instance_packing = [=, &create_instance_packing](const PackingBlock& block) {
    uint32_t pixel_sum = 0;
    uint64_t num_bits = 0;
    std::unordered_set<uint32_t> seen_pixels;
    PointerList<Chunk> instance_chunks(block.chunks().size());

    for (uint64_t i = 0; i < block.chunks().size(); ++i) {
      const Chunk* chunk = block.chunks()[i];

      if (chunk->type() == ChunkType::PIXEL_PIECE) {
        const PixelPiece& piece = Chunk::get<PixelPiece>(chunk);

        if (!seen_pixels.contains(piece.pixel_index())) {
          ++pixel_sum;
          seen_pixels.insert(piece.pixel_index());
        }
        num_bits += piece.num_bits();
        instance_chunks.emplace_back<PixelPiece>(piece);

      } else if (chunk->type() == ChunkType::PADDING) {
        const Padding& pad = Chunk::get<Padding>(chunk);

        if (pad.repeat().type == ChunkRepeatType::FINITE) {
          num_bits += pad.num_bits();
          instance_chunks.emplace_back<Padding>(pad);

        } else if (pad.repeat().type == ChunkRepeatType::FILL_STRIDE) {
          ZX_ASSERT_MSG(row_stride, "Stride needed but not provided.");
          ZX_ASSERT_MSG(
              num_bits % 8 == 0,
              "There must be an even number of bytes in a PixelBlock before FILL_STRIDE padding.");
          // If we're looking at stride padding, the current num_bits must be the total for the full
          // width. The stride should be strictly bigger then, or the format is ill specified.
          size_t bytes = num_bits / 8;
          ZX_ASSERT_MSG(
              *row_stride >= bytes,
              "Row stride must be greater than or equal to the number of bytes in 'width' pixels.");
          if (*row_stride > bytes) {
            size_t pad_size = *row_stride - bytes;
            num_bits += pad_size * 8;
            instance_chunks.emplace_back<Padding>(pad_size * 8, ChunkRepeat::finite(1));
          }

        } else {
          // It doesn't make any sense to fill an image or the width of an image with padding.
          ZX_ASSERT_MSG(false, "Padding only supports FINITE and FILL_STRIDE repeat types.");
        }

      } else if (chunk->type() == ChunkType::PACKING_BLOCK) {
        const PackingBlock& subblock = Chunk::get<PackingBlock>(chunk);
        PackingBlock subblock_instance = create_instance_packing(subblock);

        // The PackingBlock returned by any call to this function must have a FINITE repeat
        // value, as that is the point of this function.
        ZX_ASSERT(subblock_instance.repeat().type == ChunkRepeatType::FINITE);
        pixel_sum += subblock_instance.repeat().times * subblock_instance.num_pixels();
        num_bits += subblock_instance.repeat().times * subblock_instance.num_bits();
        instance_chunks.emplace_back<PackingBlock>(std::move(subblock_instance));
      }
    }

    ZX_ASSERT_MSG(num_bits % 8 == 0, "A PackingBlock must contain a whole number of bytes.");
    if (block.repeat().type == ChunkRepeatType::FINITE) {
      return PackingBlock(std::move(instance_chunks), block.repeat());
    } else if (block.repeat().type == ChunkRepeatType::FILL_WIDTH) {
      ZX_ASSERT_MSG(
          width % pixel_sum == 0,
          "A FILL_WIDTH PackingBlock must contain a number of pixels that evenly divides the "
          "width.");
      uint32_t num_blocks = width / pixel_sum;
      return PackingBlock(std::move(instance_chunks), ChunkRepeat::finite(num_blocks));
    }

    // If we've gotten here the repeat type should be FILL_IMAGE.
    ZX_ASSERT_MSG(block.repeat().type == ChunkRepeatType::FILL_IMAGE,
                  "PackingBlocks only support FINITE, FILL_WIDTH, and FILL_IMAGE repeat types.");
    uint32_t total_pixels = width * height;
    uint32_t num_blocks = total_pixels / pixel_sum;
    if (total_pixels % pixel_sum != 0) {
      ++num_blocks;
    }
    return PackingBlock(std::move(instance_chunks), ChunkRepeat::finite(num_blocks));
  };

  PackingBlock instance_block = create_instance_packing(format.packing_block);
  return RawFormatInstance(format.id, width, height, row_stride, std::move(instance_block),
                           format.color_filter, format.depth_map);
}

}  // namespace camera::raw
