// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_PTS_MANAGER_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_PTS_MANAGER_H_

#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <cstdint>
#include <map>
#include <mutex>

class PtsManager {
 public:
  // 8 is the max number of frames in a VP9 superframe.  For H264, num_reorder_frames is max 16.  So
  // 32 should be enough for both VP9 and H264.
  static constexpr uint32_t kMaxEntriesDueToFrameReordering = 32;

  // This "extra" value should take care of any buffering in the video decoder itself, and any delay
  // outputting a decompressed frame after it has been removed from the stream buffer.
  static constexpr uint32_t kMaxEntriesDueToExtraDecoderDelay = 32;

  // Large enough to store an entry per every 4 bytes of the 4k h264 stream buffer.  This assumes
  // every frame is a 3 byte start code + 1 byte NALU header and that's all.  Real frames are
  // larger, so this will be enough entries for our current worst case.
  static constexpr uint32_t kH264SingleMaxEntriesDueToStreamBuffering = 4 * 1024 / 4;

  static constexpr uint32_t kH264SingleStreamMaxEntriesToKeep =
      kMaxEntriesDueToFrameReordering + kMaxEntriesDueToExtraDecoderDelay +
      kH264SingleMaxEntriesDueToStreamBuffering;

  // Large enough to account for the <= 1024 bytes of data required by the FW when using
  // h264_multi_decoder before the FW is willing to start decoding the first available data.
  //
  // TODO(fxbug.dev/13483): Pad the data provided to FW with AUD + padding when we know we have at least
  // one frame end available so far that hasn't seen a corresponding pic data done.  Preferably
  // without relying on PtsManager though.
  static constexpr uint32_t kH264MultiMaxEntriesDueToFifo = 1024 / 4;
  // Threshold used by h264_multi_decoder to avoid over-queueing data if we've already got more than
  // enough PTS values, which should imply that frame boundaries exist, which should imply that some
  // progress can be made decoding without adding more input data.
  static constexpr uint32_t kH264MultiQueuedEntryCountThreshold =
      kH264MultiMaxEntriesDueToFifo + kMaxEntriesDueToExtraDecoderDelay;
  // Because we use kH264MultiMaxEntriesDueToFifo as a threshold for decoding more without adding
  // any new data, we need to be sure the PtsManager can definitely hold at least
  // kH264MultiMaxEntriesDueToFifo comfortably without eating into the margin provided by any of the
  // other constants, so we keep 2x as many as we really need for this reason.
  static constexpr uint32_t kH264MultiMaxEntriesDueToFifoWithMargin =
      2 * kH264MultiMaxEntriesDueToFifo;

  static constexpr uint32_t kH264MultiStreamMaxEntriesToKeep =
      kMaxEntriesDueToFrameReordering + kMaxEntriesDueToExtraDecoderDelay +
      kH264MultiMaxEntriesDueToFifoWithMargin;

  // TODO(fxbug.dev/13483): This should have its own constants, not just be the max of these other two.
  static constexpr uint32_t kVp9MaxEntriesToKeep =
      std::max(kH264SingleStreamMaxEntriesToKeep, kH264MultiStreamMaxEntriesToKeep);

  static constexpr uint32_t kMaxEntriesToKeep = std::max(
      {kH264SingleStreamMaxEntriesToKeep, kH264MultiStreamMaxEntriesToKeep, kVp9MaxEntriesToKeep});

  class LookupResult {
   public:
    // Outside of PtsManager, can only be copied, not created from scratch and
    // not assigned.
    LookupResult(const LookupResult& from) = default;

    bool is_end_of_stream() const { return is_end_of_stream_; }
    bool has_pts() const { return has_pts_; }
    uint64_t pts() const { return pts_; }

   private:
    friend class PtsManager;
    LookupResult() = delete;

    LookupResult(bool is_end_of_stream, bool has_pts, uint64_t pts)
        : is_end_of_stream_(is_end_of_stream), has_pts_(has_pts), pts_(pts) {
      // PTS == 0 is valid, but if we don't have a PTS, the field must be set to
      // 0.  In other words, we still need the sparate has_pts_ to tell whether
      // we have a PTS when the pts field is 0 - this way all pts values are
      // usable.
      ZX_DEBUG_ASSERT(has_pts_ || !pts_);
      ZX_DEBUG_ASSERT(!(is_end_of_stream_ && has_pts_));
    }

    // If is_end_of_stream_, there is no PTS.  Instead, the stream is over.
    const bool is_end_of_stream_ = false;

    // If !has_pts_, the pts_ field is not meaningful (but is set to 0).
    const bool has_pts_ = false;

    // If has_pts(), the pts field is meaningful.
    //
    // When has_pts(), the PTS of the frame.
    // When !has_pts(), 0.
    const uint64_t pts_ = 0;
  };

  void SetLookupBitWidth(uint32_t lookup_bit_width);

  // Offset is the byte offset into the stream of the beginning of the frame.
  void InsertPts(uint64_t offset, bool has_pts, uint64_t pts);

  // |end_of_stream_offset| is the first byte offset which is not part of the
  // input stream data (stream offset of last input stream byte + 1).
  void SetEndOfStreamOffset(uint64_t end_of_stream_offset);

  // Offset must be within the frame that's being looked up.
  const LookupResult Lookup(uint64_t offset);

  // Counts how many InsertPts() entries exist with offset >= threshold_offset.
  // This helps avoid queueing so much into h264_multi_decoder's PtsManager that
  // kMaxEntriesToKeep is exhausted.
  uint32_t CountEntriesBeyond(uint64_t threshold_offset) const;

 private:
  // The last inserted offset is offset_to_result_.rbegin()->first, unless empty() in which case
  // logically 0.
  uint64_t GetLastInsertedOffset() __TA_REQUIRES(lock_);
  uint32_t CountEntriesBeyondLocked(uint64_t threshold_offset) const __TA_REQUIRES(lock_);

  mutable std::mutex lock_;
  __TA_GUARDED(lock_)
  uint32_t lookup_bit_width_ = 64;

  // TODO(dustingreen): Consider switching to a SortedCircularBuffer (to be implemented) of size
  // kMaxEntries instead, to avoid so many pointers and separate heap allocations.  Despite the
  // memory inefficiency vs. a circular buffer, this likely consumes ~128KiB, so switching isn't
  // urgent.
  __TA_GUARDED(lock_)
  std::map<uint64_t, LookupResult> offset_to_result_;
};

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_PTS_MANAGER_H_
