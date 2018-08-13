// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/demux/sparse_byte_buffer.h"

#include "lib/fxl/logging.h"

namespace media_player {

SparseByteBuffer::Hole::Hole() {}

SparseByteBuffer::Hole::Hole(std::map<size_t, size_t>::iterator iter)
    : iter_(iter) {}

SparseByteBuffer::Hole::Hole(const Hole& other) : iter_(other.iter_) {}

SparseByteBuffer::Hole::~Hole() {}

SparseByteBuffer::Region::Region() {}

SparseByteBuffer::Region::Region(
    std::map<size_t, std::vector<uint8_t>>::iterator iter)
    : iter_(iter) {}

SparseByteBuffer::Region::Region(const Region& other) : iter_(other.iter_) {}

SparseByteBuffer::Region::~Region() {}

SparseByteBuffer::SparseByteBuffer() {}

SparseByteBuffer::~SparseByteBuffer() {}

void SparseByteBuffer::Initialize(size_t size) {
  holes_.clear();
  regions_.clear();
  size_ = size;
  // Create one hole spanning the entire buffer.
  holes_[0] = size_;
}

SparseByteBuffer::Region SparseByteBuffer::FindRegionContaining(size_t position,
                                                                Region hint) {
  FXL_DCHECK(size_ > 0u);
  FXL_DCHECK(position < size_);

  RegionsIter iter = hint.iter_;

  if (iter != regions_.end() && iter->first <= position) {
    if (iter->first + iter->second.size() <= position) {
      // iter is too close to the front. See if the next region is correct.
      ++iter;
      if (iter != regions_.end() && iter->first <= position &&
          position < iter->first + iter->second.size()) {
        return Region(iter);
      }
    } else if (position < iter->first + iter->second.size()) {
      return Region(iter);
    }
  }

  iter = regions_.lower_bound(position);
  if (iter != regions_.begin() &&
      (iter == regions_.end() || iter->first > position)) {
    --iter;
    FXL_DCHECK(iter->first <= position);
    if (iter->first + iter->second.size() <= position) {
      iter = regions_.end();
    }
  }

  return Region(iter);
}

SparseByteBuffer::Hole SparseByteBuffer::FindOrCreateHole(size_t position,
                                                          Hole hint) {
  FXL_DCHECK(size_ > 0u);
  FXL_DCHECK(!holes_.empty());

  HolesIter result = hint.iter_;

  if (result == holes_.end()) {
    result = holes_.begin();
  }

  if (result->first != position) {
    if (result->first > position ||
        result->first + result->second <= position) {
      // Need to find the hole containing the requested position.
      result = FindHoleContaining(position).iter_;
      FXL_DCHECK(result != holes_.end());
    }

    if (result->first != position) {
      // Need to split this hole.
      FXL_DCHECK(position > result->first);
      size_t front_size = position - result->first;

      FXL_DCHECK(result->second > front_size);
      size_t back_size = result->second - front_size;

      result->second = front_size;

      result = holes_.insert(++result,
                             std::pair<size_t, size_t>(position, back_size));
    }
  }

  FXL_DCHECK(result->first == position);

  return Hole(result);
}

SparseByteBuffer::Hole SparseByteBuffer::FindHoleContaining(size_t position) {
  FXL_DCHECK(size_ > 0u);
  HolesIter iter = holes_.lower_bound(position);
  if (iter != holes_.begin() &&
      (iter == holes_.end() || iter->first > position)) {
    --iter;
    FXL_DCHECK(iter->first <= position);
    if (iter->first + iter->second < position) {
      iter = holes_.end();
    }
  }

  return Hole(iter);
}

SparseByteBuffer::Hole SparseByteBuffer::Fill(Hole hole,
                                              std::vector<uint8_t>&& buffer) {
  FXL_DCHECK(size_ > 0u);
  FXL_DCHECK(hole.iter_ != holes_.end());
  FXL_DCHECK(buffer.size() != 0);
  FXL_DCHECK(buffer.size() <= hole.size());

  HolesIter holes_iter = hole.iter_;

  size_t buffer_size = buffer.size();
  size_t position = holes_iter->first;

  regions_.emplace(std::make_pair(position, std::move(buffer)));

  // Remove the region from holes_.
  while (buffer_size != 0) {
    FXL_DCHECK(holes_iter != holes_.end());
    FXL_DCHECK(holes_iter->first == position);

    if (buffer_size < holes_iter->second) {
      // We've filled part of *holes_iter. Insert a hole after it to
      // represent the remainder.
      HolesIter hint = holes_iter;
      holes_.insert(
          ++hint, std::pair<size_t, size_t>(holes_iter->first + buffer_size,
                                            holes_iter->second - buffer_size));

      // When we've erased holes_iter, we'll have accounted for the entire
      // filled region.
      position += buffer_size;
      buffer_size = 0;
    } else {
      // Calculate where we'll be when we've erased holes_iter.
      position += holes_iter->second;
      buffer_size -= holes_iter->second;
    }

    holes_iter = holes_.erase(holes_iter);
    if (holes_iter == holes_.end()) {
      FXL_DCHECK(buffer_size == 0);
      holes_iter = holes_.begin();
    }
  }

  return Hole(holes_iter);
}

bool operator==(const SparseByteBuffer::Hole& a,
                const SparseByteBuffer::Hole& b) {
  return a.iter_ == b.iter_;
}

bool operator!=(const SparseByteBuffer::Hole& a,
                const SparseByteBuffer::Hole& b) {
  return a.iter_ != b.iter_;
}

bool operator==(const SparseByteBuffer::Region& a,
                const SparseByteBuffer::Region& b) {
  return a.iter_ == b.iter_;
}

bool operator!=(const SparseByteBuffer::Region& a,
                const SparseByteBuffer::Region& b) {
  return a.iter_ != b.iter_;
}

}  // namespace media_player
