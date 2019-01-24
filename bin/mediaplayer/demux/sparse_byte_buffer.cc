// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <iterator>

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

size_t SparseByteBuffer::ReadRange(size_t start, size_t size,
                                   uint8_t* dest_buffer) {
  FXL_DCHECK(start < size_);
  FXL_DCHECK(dest_buffer != nullptr);

  size_t copied = 0;
  size_t end = start + size;

  RegionsIter iter = regions_.lower_bound(start);
  if (iter != regions_.begin()) {
    --iter;
  }

  size_t last_region_end = iter->first;
  while (iter != regions_.end() && iter->first == last_region_end &&
         iter->first < end) {
    size_t region_start = iter->first;
    size_t region_end = iter->first + iter->second.size();

    if (region_end > start) {
      size_t offset_in_dest = region_start > start ? region_start - start : 0;
      size_t offset_in_source = region_start < start ? start - region_start : 0;
      size_t bytes_to_copy =
          std::min(region_end, end) - region_start - offset_in_source;
      std::memcpy(dest_buffer + offset_in_dest,
                  iter->second.data() + offset_in_source, bytes_to_copy);
      copied += bytes_to_copy;
    }

    last_region_end = region_end;
    ++iter;
  }

  return copied;
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

size_t SparseByteBuffer::BytesStored() {
  size_t bytes_stored = 0;
  for (auto& region : regions_) {
    bytes_stored += region.second.size();
  }
  return bytes_stored;
}

std::optional<size_t> SparseByteBuffer::NextMissingByte(size_t start) {
  if (FindHoleContaining(start) != null_hole()) {
    return start;
  }

  HolesIter iter = holes_.lower_bound(start);
  if (iter == holes_.end()) {
    return std::nullopt;
  }

  return std::max(iter->first, start);
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

  if (iter == holes_.end() || iter->first + iter->second <= position ||
      iter->first > position) {
    return null_hole();
  }

  return Hole(iter);
}

std::vector<SparseByteBuffer::Hole> SparseByteBuffer::FindOrCreateHolesInRange(
    size_t start, size_t size) {
  FXL_DCHECK(start < size_);

  std::vector<Hole> holes_in_range;

  size_t end = start + size;
  HolesIter iter = holes_.lower_bound(start);
  if (iter != holes_.begin()) {
    --iter;
  }

  while (iter != holes_.end() && iter->first < end) {
    size_t hole_start = iter->first;
    size_t hole_end = iter->first + iter->second;
    if (hole_end > start) {
      if (hole_end > end) {
        size_t trailing = hole_end - end;
        holes_.emplace(end, trailing);
        iter->second -= trailing;
      }

      if (hole_start < start) {
        // Truncate this hole and add a new one with
        // only the subset inside our range, which we will
        // evaluate on the next loop iteration.
        size_t preceding = start - hole_start;
        holes_.emplace(start, iter->second - preceding);
        iter->second = preceding;
      } else {
        holes_in_range.push_back(Hole(iter));
      }
    }
    ++iter;
  }

  return holes_in_range;
}

size_t SparseByteBuffer::BytesMissingInRange(size_t start, size_t size) {
  FXL_DCHECK(start < size_);

  size_t bytes_missing = 0;

  size_t end = start + size;
  HolesIter iter = holes_.lower_bound(start);
  if (iter != holes_.begin()) {
    --iter;
  }

  while (iter != holes_.end() && iter->first < end) {
    size_t hole_start = iter->first;
    size_t hole_end = iter->first + iter->second;
    if (hole_end > start) {
      size_t extraneous = 0;
      if (hole_end > end) {
        size_t trailing = hole_end - end;
        extraneous += trailing;
      }

      if (hole_start < start) {
        size_t preceding = start - hole_start;
        extraneous += preceding;
      }

      bytes_missing += iter->second - extraneous;
    }
    ++iter;
  }

  return bytes_missing;
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

size_t SparseByteBuffer::CleanUpExcept(size_t goal, size_t protected_start,
                                       size_t protected_size) {
  FXL_DCHECK(protected_start < size_);

  if (regions_.empty()) {
    return 0;
  }

  size_t to_free = goal;
  size_t protected_end = protected_start + protected_size;

  // First we clean up regions before the protected range, prioritizing clean up
  // of regions farther from the range.
  RegionsIter iter = regions_.begin();
  while (to_free > 0 && iter != regions_.end() &&
         iter->first < protected_start) {
    Region candidate = Region(iter);
    iter++;

    size_t excess_before = candidate.position() < protected_start
                               ? protected_start - candidate.position()
                               : 0;
    size_t shrink_amount = std::min({to_free, candidate.size(), excess_before});
    ShrinkRegionFront(candidate, shrink_amount);
    to_free -= shrink_amount;
  }

  if (regions_.empty()) {
    FXL_DCHECK(goal >= to_free);
    return goal - to_free;
  }

  // Second and lastly we clean up regions after the protected range,
  // prioritizing clean up of regions farther from the range.
  iter = --regions_.end();
  bool seen_last_region = false;
  while (to_free > 0 && iter->first >= protected_start && !seen_last_region) {
    seen_last_region = iter == regions_.begin();
    Region candidate = Region(iter);
    if (!seen_last_region) {
      --iter;
    }

    size_t candidate_end = candidate.position() + candidate.size();
    size_t excess_after =
        protected_end < candidate_end ? candidate_end - protected_end : 0;
    size_t shrink_amount = std::min({to_free, candidate.size(), excess_after});
    ShrinkRegionBack(candidate, shrink_amount);
    to_free -= shrink_amount;
  }

  FXL_DCHECK(goal >= to_free);
  return goal - to_free;
}

SparseByteBuffer::Region SparseByteBuffer::ShrinkRegionFront(
    Region region, size_t shrink_amount) {
  FXL_DCHECK(region != null_region());

  if (shrink_amount >= region.size()) {
    Free(region);
    return null_region();
  }

  if (shrink_amount == 0) {
    return region;
  }

  Hole hole_before = null_hole();
  if (region.position() > 0) {
    hole_before = FindHoleContaining(region.position() - 1);
  }

  if (hole_before != null_hole()) {
    hole_before.iter_->second += shrink_amount;
  } else {
    holes_.emplace(region.position(), shrink_amount);
  }

  size_t region_pos = region.position() + shrink_amount;
  std::vector<uint8_t> buffer;
  std::copy_n(region.iter_->second.begin() + shrink_amount,
              region.size() - shrink_amount, std::back_inserter(buffer));
  regions_.erase(region.iter_);
  auto result = regions_.emplace(region_pos, buffer);
  FXL_DCHECK(result.second);

  return Region(result.first);
}

SparseByteBuffer::Region SparseByteBuffer::ShrinkRegionBack(
    Region region, size_t shrink_amount) {
  FXL_DCHECK(region != null_region());

  if (shrink_amount >= region.size()) {
    Free(region);
    return null_region();
  }

  if (shrink_amount == 0) {
    return region;
  }

  size_t hole_addendum = 0;
  Hole hole_after = FindHoleContaining(region.position() + region.size());
  if (hole_after != null_hole()) {
    hole_addendum = hole_after.size();
    holes_.erase(hole_after.iter_);
  }

  holes_.emplace(region.position() + region.size() - shrink_amount,
                 shrink_amount + hole_addendum);
  region.iter_->second.resize(region.size() - shrink_amount);

  return region;
}

SparseByteBuffer::Hole SparseByteBuffer::Free(Region region) {
  FXL_DCHECK(region != null_region());

  Hole hole_before = null_hole();
  if (region.position() > 0) {
    hole_before = FindHoleContaining(region.position() - 1);
  }

  Hole hole_after = FindHoleContaining(region.position() + region.size());

  Hole new_hole = null_hole();
  size_t hole_position = region.position();
  size_t hole_size = region.size();

  regions_.erase(region.iter_);

  if (hole_after != null_hole() &&
      hole_after.position() == hole_position + hole_size) {
    hole_size += hole_after.size();
    holes_.erase(hole_after.iter_);
  }

  if (hole_before != null_hole() && hole_before.position() <= hole_position) {
    hole_size += hole_before.size();
    new_hole = hole_before;
    new_hole.iter_->second = hole_size;
  } else {
    auto result = holes_.emplace(hole_position, hole_size);
    FXL_DCHECK(result.second);
    new_hole = Hole(result.first);
  }

  return new_hole;
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
