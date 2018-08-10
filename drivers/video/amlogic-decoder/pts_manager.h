// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_PTS_MANAGER_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_PTS_MANAGER_H_

#include <lib/fxl/logging.h>
#include <lib/fxl/synchronization/thread_annotations.h>

#include <cstdint>
#include <map>
#include <mutex>

class PtsManager {
 public:
  class LookupResult {
   public:
    // Outside of PtsManager, can only be copied, not created from scratch and
    // not assigned.
    LookupResult(const LookupResult& from) = default;

    bool is_end_of_stream() const { return is_end_of_stream_; }
    bool has_pts() const { return !is_end_of_stream_; }
    uint64_t pts() const { return pts_; }

   private:
    friend class PtsManager;
    LookupResult() = delete;
    LookupResult& operator=(const LookupResult& from) = default;

    LookupResult(bool is_end_of_stream, bool has_pts, uint64_t pts)
        : is_end_of_stream_(is_end_of_stream), has_pts_(has_pts), pts_(pts) {
      // PTS == 0 is valid, but if we don't have a PTS, the field must be set to
      // 0.  In other words, we still need the sparate has_pts_ to tell whether
      // we have a PTS when the pts field is 0 - this way all pts values are
      // usable.
      FXL_DCHECK(has_pts_ || !pts_);
      FXL_DCHECK(!(is_end_of_stream_ && has_pts_));
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

  // Offset is the byte offset into the stream of the beginning of the frame.
  void InsertPts(uint64_t offset, uint64_t pts);

  // |end_of_stream_offset| is the first byte offset which is not part of the
  // input stream data (stream offset of last input stream byte + 1).
  void SetEndOfStreamOffset(uint64_t end_of_stream_offset);

  // Offset must be within the frame that's being looked up. Only the last 100
  // PTS inserted are kept around (last 100 by stream offset).
  const LookupResult Lookup(uint64_t offset);

 private:
  std::mutex lock_;
  FXL_GUARDED_BY(lock_)
  std::map<uint64_t, LookupResult> offset_to_result_;
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_PTS_MANAGER_H_
