// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <lib/fxl/logging.h>

#include "sliding_buffer.h"

namespace media_player {

SlidingBuffer::SlidingBuffer(size_t capacity) : store_(capacity, 0) {}

SlidingBuffer::~SlidingBuffer() {}

size_t SlidingBuffer::Read(size_t pos, uint8_t* buffer, size_t bytes_to_read) {
  if (pos < filled_range_.start || pos >= filled_range_.end()) {
    return 0;
  }

  const size_t read_size = std::min(bytes_to_read, filled_range_.end() - pos);
  const Range read_range = {.start = pos, .length = read_size};
  size_t bytes_read = 0;
  for (const auto& block : BlocksInRange(read_range)) {
    memcpy(buffer + bytes_read, block.buffer, block.size);
    bytes_read += block.size;
  }

  FXL_DCHECK(read_size == bytes_read);
  return read_size;
}

std::vector<SlidingBuffer::Block> SlidingBuffer::Slide(size_t dest_pos,
                                                       size_t budget) {
  FXL_DCHECK(budget <= store_.size())
      << budget << " bytes were requested but buffer has capacity of "
      << store_.size() << ".";

  const Range desired_range = FindNewRange(dest_pos, budget);

  std::vector<SlidingBuffer::Block> blocks;
  for (const auto& hole : ClipRange(desired_range, filled_range_)) {
    for (const auto& block : BlocksInRange(hole)) {
      blocks.push_back(block);
    }
  }

  filled_range_ = desired_range;
  return blocks;
}

// static
std::vector<SlidingBuffer::Range> SlidingBuffer::ClipRange(
    const SlidingBuffer::Range& base, const SlidingBuffer::Range& clip) {
  const size_t clip_end = clip.start + clip.length;
  const size_t base_end = base.start + base.length;

  std::vector<Range> ranges;
  if (clip.start > base.start && clip.start <= base_end) {
    ranges.push_back({.start = base.start, .length = clip.start - base.start});
  }

  if (clip_end < base_end && clip_end >= base.start) {
    ranges.push_back({.start = clip_end, .length = base_end - clip_end});
  }

  if (ranges.empty()) {
    // Clipping range does not overlap with base; all of base is left unclipped.
    return {base};
  }

  return ranges;
}

SlidingBuffer::Range SlidingBuffer::FindNewRange(size_t dest_pos,
                                                 size_t budget) {
  Range desired_range = {.start = dest_pos, .length = budget};

  if (desired_range.end() < filled_range_.end() &&
      desired_range.end() >= filled_range_.start &&
      desired_range.length < store_.size()) {
    desired_range.length += std::min(filled_range_.end() - desired_range.end(),
                                     store_.size() - desired_range.length);
  }

  if (desired_range.start > filled_range_.start &&
      desired_range.start <= filled_range_.end() &&
      desired_range.length < store_.size()) {
    const size_t expansion = std::min(desired_range.start - filled_range_.start,
                                      store_.size() - desired_range.length);
    desired_range.length += expansion;
    desired_range.start -= expansion;
  }

  return desired_range;
}

std::vector<SlidingBuffer::Block> SlidingBuffer::BlocksInRange(
    const Range& range) {
  const size_t start = range.start % store_.size();
  const size_t end = std::min(start + range.length, store_.size());
  const size_t wrap_end = (start + range.length) % store_.size();

  std::vector<SlidingBuffer::Block> blocks = {
      {.start = range.start, .size = end - start, .buffer = &store_[start]}};
  if (start + range.length > store_.size()) {
    blocks.push_back({.start = range.start + (end - start),
                      .size = wrap_end,
                      .buffer = &store_[0]});
  }

  return blocks;
}

}  // namespace media_player
