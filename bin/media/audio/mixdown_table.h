// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <iterator>
#include <memory>

#include "garnet/bin/media/audio/level.h"
#include "lib/fxl/logging.h"

namespace media {

// A mutable rectangular matrix of Levels indicating how input channels should
// be mixed to ouput channels.
template <typename TLevel>
class MixdownTable {
 public:
  class iterator
      : public std::iterator<std::forward_iterator_tag, Level<TLevel>> {
   public:
    explicit iterator(Level<TLevel>* ptr) : ptr_(ptr) {}
    iterator& operator++() {
      ++ptr_;
      return *this;
    }
    iterator operator++(int) {
      iterator retval = *this;
      ++ptr_;
      return retval;
    }
    bool operator==(iterator other) const { return ptr_ == other.ptr_; }
    bool operator!=(iterator other) const { return !(*this == other); }
    Level<TLevel>& operator*() const { return *ptr_; }
    Level<TLevel>& operator->() const { return *ptr_; }

    Level<TLevel>* ptr_;
  };

  // Creates a mixdown table that produces silence on all output channels.
  static std::unique_ptr<MixdownTable<TLevel>> CreateSilent(
      uint32_t in_channel_count,
      uint32_t out_channel_count) {
    return std::make_unique<MixdownTable>(in_channel_count, out_channel_count);
  }

  // Creates a square mixdown table that passes input channels through to
  // respective output channels with a change in level.
  static std::unique_ptr<MixdownTable<TLevel>> CreateLevelChange(
      uint32_t channel_count,
      Level<TLevel> level) {
    std::unique_ptr<MixdownTable> table =
        CreateSilent(channel_count, channel_count);

    for (uint32_t channel = 0; channel < channel_count; ++channel) {
      table->get_level(channel, channel) = level;
    }

    return table;
  }

  // Creates a square mixdown table that passes input channels through to
  // respective output channels unchanged.
  static std::unique_ptr<MixdownTable<TLevel>> CreatePassthrough(
      uint32_t channel_count) {
    return CreateLevelChange(channel_count, Level<TLevel>::Unity);
  }

  MixdownTable(uint32_t in_channel_count, uint32_t out_channel_count)
      : in_channel_count_(in_channel_count),
        out_channel_count_(out_channel_count),
        levels_(new Level<TLevel>[in_channel_count * out_channel_count]) {}

  // Returns the number of input channels.
  uint32_t in_channel_count() { return in_channel_count_; }

  // Returns the number of output channels.
  uint32_t out_channel_count() { return out_channel_count_; }

  // Gets a non-const reference to the level applied to in_channel when mixing
  // to out_channel.
  Level<TLevel>& get_level(uint32_t in_channel, uint32_t out_channel) {
    FXL_DCHECK(in_channel < in_channel_count_);
    FXL_DCHECK(out_channel < out_channel_count_);
    // Level indexing needs to happen such that when iterating across all levels
    // with an outer out_channel loop and an inner in_channel loop, simply
    // incrementing a Level pointer is adequate.
    return levels_[in_channel + out_channel * in_channel_count_];
  }

  // Returns a forward iterator. All levels in the table can be enumerated by
  // incrementing this iterator in an outer loop of output channels and an inner
  // loop of input channels.
  iterator begin() { return iterator(&levels_[0]); }

  iterator end() {
    return iterator(&levels_[in_channel_count_ * out_channel_count_]);
  }

 private:
  uint32_t in_channel_count_;
  uint32_t out_channel_count_;
  std::unique_ptr<Level<TLevel>[]> levels_;
};

}  // namespace media
