// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CAMERA_LIB_RAW_FORMATS_RAW_PACKING_H_
#define SRC_CAMERA_LIB_RAW_FORMATS_RAW_PACKING_H_

#include <bit>
#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "src/camera/lib/raw_formats/pointer_list.h"
#include "src/camera/lib/raw_formats/raw_hash.h"

namespace camera::raw {

// A chunk may be:
// - some number of bits of padding.
// - some number of bits (a "piece") of a pixel.
// - a packing block containing a list of chunks.
enum class ChunkType {
  PADDING,
  PIXEL_PIECE,
  PACKING_BLOCK,
};

// A chunk can be tagged to be repeated in certain ways within the context of it's parent.
enum class ChunkRepeatType {
  FINITE,
  FILL_WIDTH,
  FILL_STRIDE,
  FILL_IMAGE,
};

// All the information needed to describe chunk repetition.
struct ChunkRepeat {
  ChunkRepeatType type;
  uint32_t times;  // used if type is FINITE.

  static constexpr ChunkRepeat finite(uint32_t times) { return {ChunkRepeatType::FINITE, times}; }
  static constexpr ChunkRepeat fill_width() { return {ChunkRepeatType::FILL_WIDTH, 0}; }
  static constexpr ChunkRepeat fill_stride() { return {ChunkRepeatType::FILL_STRIDE, 0}; }
  static constexpr ChunkRepeat fill_image() { return {ChunkRepeatType::FILL_IMAGE, 0}; }
};

// Interface for a "Chunk". All chunks have a size in bits, some repetition specifier, and a type
// (used for casting to the actual implementation). They should also be cloneable (performs dynamic
// allocation).
class Chunk {
 public:
  constexpr virtual ~Chunk() = default;

  constexpr virtual uint64_t num_bits() const = 0;
  constexpr virtual ChunkType type() const = 0;
  constexpr virtual ChunkRepeat repeat() const = 0;

  constexpr virtual Chunk* clone() const = 0;

  // A helper for grabbing a const reference to the underlying implementation.
  template <typename T>
    requires std::derived_from<T, Chunk>
  static constexpr const T& get(const Chunk* chunk) {
    return *(static_cast<const T*>(chunk));
  }
};

class Padding : public Chunk {
 public:
  constexpr Padding(uint64_t num_bits, ChunkRepeat repeat) : num_bits_(num_bits), repeat_(repeat) {}

  constexpr Padding(const Padding& o) : num_bits_(o.num_bits_), repeat_(o.repeat_) {}

  constexpr uint64_t num_bits() const override { return num_bits_; }
  constexpr ChunkType type() const override { return type_; }
  constexpr ChunkRepeat repeat() const override { return repeat_; }

  constexpr Chunk* clone() const override { return new Padding(num_bits_, repeat_); }

 private:
  const uint64_t num_bits_;
  const ChunkType type_ = ChunkType::PADDING;
  const ChunkRepeat repeat_;
};

class PixelPiece : public Chunk {
 public:
  constexpr PixelPiece(uint32_t pixel_index, uint8_t mask, int8_t shift)
      : pixel_index_(pixel_index), num_bits_(std::popcount(mask)), mask_(mask), shift_(shift) {}

  constexpr PixelPiece(const PixelPiece& o)
      : pixel_index_(o.pixel_index_), num_bits_(o.num_bits_), mask_(o.mask_), shift_(o.shift_) {}

  constexpr uint32_t pixel_index() const { return pixel_index_; }
  constexpr uint8_t mask() const { return mask_; }
  constexpr int8_t shift() const { return shift_; }

  constexpr uint64_t num_bits() const override { return num_bits_; }
  constexpr ChunkType type() const override { return ChunkType::PIXEL_PIECE; }
  constexpr ChunkRepeat repeat() const override { return {ChunkRepeatType::FINITE, 1}; }

  constexpr Chunk* clone() const override { return new PixelPiece(pixel_index_, mask_, shift_); }

 private:
  const uint32_t pixel_index_;
  const uint64_t num_bits_;
  const uint8_t mask_;
  const int8_t shift_;
};

// Sum all of the bits within a list of chunks, provided all of the chunks have a finite repeat
// specification. Returns zero if any are not finite.
constexpr uint64_t SumBits(const PointerList<Chunk>& chunks) {
  uint64_t sum = 0;
  for (uint64_t i = 0; i < chunks.size(); ++i) {
    const Chunk& chunk = *(chunks[i]);
    if (chunk.repeat().type != ChunkRepeatType::FINITE)
      return 0;
    sum += chunk.repeat().times * chunk.num_bits();
  }
  return sum;
}

// Sum all of the pixels within a list of chunks, provided all of the chunks have a finite repeat
// specification. Returns zero if any are not finite.
constexpr uint32_t SumPixels(const PointerList<Chunk>& chunks);

class PackingBlock : public Chunk {
 public:
  template <typename Cpl>
  constexpr PackingBlock(Cpl&& chunks, ChunkRepeat repeat)
      : size_(SumBits(chunks) / 8),
        num_pixels_(SumPixels(chunks)),
        repeat_(repeat),
        chunks_(std::forward<Cpl>(chunks)) {}

  constexpr PackingBlock(const PackingBlock& o)
      : size_(o.size_), num_pixels_(o.num_pixels_), repeat_(o.repeat_), chunks_(o.chunks_) {}

  PackingBlock(PackingBlock&& o) noexcept
      : size_(o.size_),
        num_pixels_(o.num_pixels_),
        repeat_(o.repeat_),
        chunks_(std::move(o.chunks_)) {}

  PackingBlock(PackingBlock&& o, ChunkRepeat repeat)
      : size_(o.size_),
        num_pixels_(o.num_pixels_),
        repeat_(repeat),
        chunks_(std::move(o.chunks_)) {}

  // A PackingBlock must contain a whole number of pixels.
  constexpr uint32_t num_pixels() const { return num_pixels_; }
  // A PackingBlock must be a whole number of bytes.
  constexpr size_t size() const { return size_; }
  // Get the child chunks.
  constexpr const PointerList<Chunk>& chunks() const { return chunks_; }

  // The size() in bytes multiplied by 8.
  constexpr uint64_t num_bits() const override { return size_ * 8; }
  // The type of chunk this is.
  constexpr ChunkType type() const override { return ChunkType::PACKING_BLOCK; }
  // How should this chunk be repeated.
  constexpr ChunkRepeat repeat() const override { return repeat_; }

  constexpr Chunk* clone() const override { return new PackingBlock(chunks_, repeat_); }

 private:
  size_t size_;
  uint32_t num_pixels_;
  ChunkRepeat repeat_;
  PointerList<Chunk> chunks_;
};

constexpr uint32_t SumPixels(const PointerList<Chunk>& chunks) {
  // The things we do for the sake of making it work in constexpr. In the absence of constexpr
  // support for std::unordered_set, this uses an array of bools to keep track of which pixel
  // indices we've seen. Pixel indices in a block always sequential starting at 0, and there can't
  // be more pixels than there are chunks (so this array may be slightly overprovisioned but never
  // underprovisioned).
  bool* seen_pixels = new bool[chunks.size()];
  for (uint64_t i = 0; i < chunks.size(); ++i) {
    seen_pixels[i] = false;
  }

  uint32_t pixel_sum = 0;
  for (uint64_t i = 0; i < chunks.size(); ++i) {
    const Chunk& chunk = *(chunks[i]);
    if (chunk.repeat().type != ChunkRepeatType::FINITE) {
      delete[] seen_pixels;
      return 0;
    }

    if (chunk.type() == ChunkType::PACKING_BLOCK) {
      const PackingBlock& block = Chunk::get<PackingBlock>(chunks[i]);
      pixel_sum += block.repeat().times * block.num_pixels();
    } else if (chunk.type() == ChunkType::PIXEL_PIECE) {
      const PixelPiece& piece = Chunk::get<PixelPiece>(chunks[i]);
      if (!seen_pixels[piece.pixel_index()]) {
        ++pixel_sum;
        seen_pixels[piece.pixel_index()] = true;
      }
    }
  }

  delete[] seen_pixels;
  return pixel_sum;
}

namespace internal {

template <>
struct hash<ChunkRepeat> {
  constexpr size_t operator()(ChunkRepeat const& cr) const noexcept {
    size_t seed = 0;
    internal::hash_combine(seed, cr.type);
    internal::hash_combine(seed, cr.times);
    return seed;
  }
};

template <>
struct hash<Padding> {
  constexpr size_t operator()(Padding const& c) const noexcept {
    size_t seed = 0;
    internal::hash_combine(seed, c.num_bits());
    internal::hash_combine(seed, c.type());
    internal::hash_combine(seed, c.repeat());
    return seed;
  }
};

template <>
struct hash<PixelPiece> {
  constexpr size_t operator()(PixelPiece const& p) const noexcept {
    size_t seed = 0;
    internal::hash_combine(seed, p.num_bits());
    internal::hash_combine(seed, p.type());
    internal::hash_combine(seed, p.repeat());
    internal::hash_combine(seed, p.pixel_index());
    internal::hash_combine(seed, p.mask());
    internal::hash_combine(seed, p.shift());
    return seed;
  }
};

template <>
struct hash<PackingBlock> {
  constexpr size_t operator()(PackingBlock const& pb) const noexcept {
    size_t seed = 0;
    internal::hash_combine(seed, pb.num_bits());
    internal::hash_combine(seed, pb.type());
    internal::hash_combine(seed, pb.repeat());
    internal::hash_combine(seed, pb.num_pixels());
    internal::hash_combine(seed, pb.chunks().size());
    for (uint64_t i = 0; i < pb.chunks().size(); ++i) {
      const auto& chunk = pb.chunks()[i];
      if (chunk->type() == ChunkType::PADDING) {
        const Padding& pad = Chunk::get<Padding>(chunk);
        internal::hash_combine(seed, pad);
      } else if (chunk->type() == ChunkType::PIXEL_PIECE) {
        const PixelPiece& piece = Chunk::get<PixelPiece>(chunk);
        internal::hash_combine(seed, piece);
      } else if (chunk->type() == ChunkType::PACKING_BLOCK) {
        const PackingBlock& block = Chunk::get<PackingBlock>(chunk);
        internal::hash_combine(seed, block);
      }
    }
    return seed;
  }
};

}  // namespace internal
}  // namespace camera::raw

#endif  // SRC_CAMERA_LIB_RAW_FORMATS_RAW_PACKING_H_
