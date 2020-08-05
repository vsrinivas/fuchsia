// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pts_manager.h"

#include <inttypes.h>
#include <zircon/assert.h>

#include "macros.h"

#ifndef AMLOGIC_PTS_DLOG_ENABLE
#define AMLOGIC_PTS_DLOG_ENABLE 0
#endif

#define PTS_DLOG(fmt, ...)           \
  do {                               \
    if (AMLOGIC_PTS_DLOG_ENABLE) {   \
      LOG(INFO, fmt, ##__VA_ARGS__); \
    }                                \
  } while (0)

// h264 has HW stream offset counter with 0xfffffff max - 28 bit - 256 MiB cycle period
// vp9 has a 32 bit stream offset counter.
void PtsManager::SetLookupBitWidth(uint32_t lookup_bit_width) {
  PTS_DLOG("SetLookupBitWidth() lookup_bit_width: %u", lookup_bit_width);
  std::lock_guard<std::mutex> lock(lock_);
  ZX_DEBUG_ASSERT(lookup_bit_width_ == 64 && lookup_bit_width != 64);
  lookup_bit_width_ = lookup_bit_width;
}

void PtsManager::InsertPts(uint64_t offset, bool has_pts, uint64_t pts) {
  PTS_DLOG("InsertPts() offset: %" PRIx64 " has_pts: %d pts: %" PRIx64, offset, has_pts, pts);
  std::lock_guard<std::mutex> lock(lock_);

  ZX_DEBUG_ASSERT(has_pts || !pts);

  // caller should not insert duplicates
  ZX_DEBUG_ASSERT(offset_to_result_.find(offset) == offset_to_result_.end());
  // caller should set offsets in order
  ZX_DEBUG_ASSERT(offset_to_result_.empty() || offset > offset_to_result_.rbegin()->first);

  offset_to_result_.emplace(std::make_pair(offset, LookupResult(false, has_pts, pts)));

  // Erase the oldest PTSes.  See the definition of kMaxEntriesToKeep for how we know this will be
  // enough entries.
  while (offset_to_result_.size() > kMaxEntriesToKeep) {
    offset_to_result_.erase(offset_to_result_.begin());
  }
}

void PtsManager::SetEndOfStreamOffset(uint64_t end_of_stream_offset) {
  PTS_DLOG("SetEndOfStreamOffset() end_of_stream_offset: %" PRIx64, end_of_stream_offset);
  std::lock_guard<std::mutex> lock(lock_);

  // caller should not insert duplicates
  ZX_DEBUG_ASSERT(offset_to_result_.find(end_of_stream_offset) == offset_to_result_.end());
  // caller should set offsets in order
  ZX_DEBUG_ASSERT(offset_to_result_.empty() ||
                  end_of_stream_offset > (*offset_to_result_.rbegin()).first);

  // caller should only set end of stream offset once
  ZX_DEBUG_ASSERT(offset_to_result_.empty() ||
                  !(*offset_to_result_.rbegin()).second.is_end_of_stream());

  offset_to_result_.emplace(std::make_pair(end_of_stream_offset, LookupResult(true, false, 0)));
}

const PtsManager::LookupResult PtsManager::Lookup(uint64_t offset) {
  std::lock_guard<std::mutex> lock(lock_);
  ZX_DEBUG_ASSERT(lookup_bit_width_ == 64 ||
                  offset < (static_cast<uint64_t>(1) << lookup_bit_width_));

  // last_inserted_offset is known-good in the sense that it's known to be a valid full-width
  // uint64_t input stream offset.  We prefer to anchor on this value rather than incrementally
  // anchoring on the last bit-extended offset passed in as a query, since we know with higher
  // certainty that this value is correct (and both those options are fairly near the bit-extended
  // form of the logical offset coming into this method).
  uint64_t last_inserted_offset = GetLastInsertedOffset();

  // Basically we're determining whether offset is logically above or logically below
  // last_inserted_offset.
  //
  // Shift up to the top bits of the uint64_t, so we can exploit subtraction that underflows to
  // compute distance regardless of recent overflow of a and/or b.  We could probably also do this
  // by chopping off some top order bits after subtraction, but somehow this makes more sense to me.
  // This way, we're sorta just creating a and b which are each 64 bit counters with 64 bit natural
  // overflow, so we can figure out the logical above/below relationship between offset and
  // last_inserted_offset.
  uint64_t a = last_inserted_offset << (64 - lookup_bit_width_);
  uint64_t b = offset << (64 - lookup_bit_width_);
  // Is the distance between a and b smaller if we assume b is logically above a, or if we assume
  // a is logically above b.  We want to assume the option which has a and b closer together in
  // distance on a mod ring, as we don't generally know whether offset will be logically above or
  // logically below last_inserted_offset.
  //
  // One of these will be relatively small, and the other will be huge (or both 0).  Another way to
  // do this is to check if b - a is < 0x8000000000000000.
  if (b - a <= a - b) {
    // offset is logically above (or equal to) last_inserted_offset
    offset = last_inserted_offset + ((b - a) >> (64 - lookup_bit_width_));
  } else {
    // offset is logically below last_inserted_offset
    offset = last_inserted_offset - ((a - b) >> (64 - lookup_bit_width_));
  }

  auto it = offset_to_result_.upper_bound(offset);
  // Check if this offset is < any element in the list.
  if (it == offset_to_result_.begin()) {
    PTS_DLOG("it == offset_to_result_.begin() -- offset: %" PRIx64, offset);
    return PtsManager::LookupResult(false, false, 0);
  }
  // Decrement to find the pts corresponding to the last offset <= |offset|.
  --it;

  if (AMLOGIC_PTS_DLOG_ENABLE) {
    const PtsManager::LookupResult& result = it->second;
    if (result.is_end_of_stream()) {
      PTS_DLOG("Lookup() offset: %" PRIx64 " EOS", offset);
    } else {
      PTS_DLOG("Lookup() offset: %" PRIx64 " has_pts: %d pts: %" PRIx64, offset, result.has_pts(),
               result.pts());
    }
  }

  return it->second;
}

uint32_t PtsManager::CountEntriesBeyond(uint64_t threshold_offset) const {
  std::lock_guard<std::mutex> lock(lock_);
  // Shorter bit width not implemented for this method yet.
  ZX_DEBUG_ASSERT(lookup_bit_width_ == 64);
  auto it = offset_to_result_.upper_bound(threshold_offset);
  uint32_t count = 0;
  while (it != offset_to_result_.end()) {
    ++it;
    ++count;
  }
  return count;
}

// The last inserted offset is offset_to_result_.rbegin()->first, unless empty() in which case
// logically 0.
uint64_t PtsManager::GetLastInsertedOffset() {
  if (offset_to_result_.empty()) {
    return 0;
  }
  return offset_to_result_.rbegin()->first;
}
