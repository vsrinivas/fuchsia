// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_PROCESSING_POSITION_MANAGER_H_
#define SRC_MEDIA_AUDIO_LIB_PROCESSING_POSITION_MANAGER_H_

#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/lib/format2/fixed.h"

namespace media_audio {

// Class that handles the updating of source and destination positions, as a resampler steps through
// source buffers with a specific step size (based on the resampling ratio). This class extracts a
// significant amount of duplicate code across the samplers.
class PositionManager {
 public:
  PositionManager(int32_t source_channel_count, int32_t dest_channel_count, int64_t positive_length,
                  int64_t negative_length);
  ~PositionManager() = default;

  // Non-copyable and non-movable.
  PositionManager(const PositionManager& other) = delete;
  PositionManager& operator=(const PositionManager& other) = delete;
  PositionManager(PositionManager&& other) = delete;
  PositionManager& operator=(PositionManager&& other) = delete;

  // Validates source and destination frame positions.
  static void CheckPositions(int64_t dest_frame_count, int64_t* dest_offset_ptr,
                             int64_t source_frame_count, int64_t source_offset,
                             int64_t frac_pos_filter_length, int64_t frac_step_size,
                             uint64_t rate_modulo, uint64_t denominator,
                             uint64_t source_pos_modulo);

  // Used for debugging purposes only.
  void Display() const;
  void DisplayUpdate() const;

  // Establishes the parameters for this source and dest
  void SetSourceValues(const void* source_void_ptr, int64_t source_frame_count,
                       Fixed* source_offset_ptr);
  void SetDestValues(float* dest_ptr, int64_t dest_frame_count, int64_t* dest_offset_ptr);

  // Specifies the rate parameters. If not called, a unity rate (1:1) is assumed.
  void SetRateValues(int64_t step_size, uint64_t rate_modulo, uint64_t denominator,
                     uint64_t source_pos_modulo);

  // Retrieves the pointer to the current source frame (based on source offset).
  template <typename SourceSampleType>
  SourceSampleType* CurrentSourceFrame() const {
    FX_CHECK(frac_source_offset_ >= 0);

    auto source_ptr = static_cast<SourceSampleType*>(source_void_ptr_);
    return source_ptr +
           (frac_source_offset_ >> Fixed::Format::FractionalBits) * source_channel_count_;
  }

  // Retrieves the pointer to the current destination frame (based on destination offset).
  float* CurrentDestFrame() const { return dest_ptr_ + (dest_offset_ * dest_channel_count_); }

  // Returns true if there is enough remaining source data and destination space to produce another
  // frame.
  bool CanFrameBeMixed() const {
    return (dest_offset_ < dest_frame_count_) && (frac_source_offset_ < frac_source_end_);
  }

  // Returns true if there is NOT enough remaining source data to produce another output frame.
  bool IsSourceConsumed() const { return (frac_source_offset_ >= frac_source_end_); }

  // Advances one dest frame (and related source, incl modulo); return the new source_offset.
  int64_t AdvanceFrame() {
    ++dest_offset_;
    frac_source_offset_ += frac_step_size_;

    source_pos_modulo_ += rate_modulo_;
    if (source_pos_modulo_ >= denominator_) {
      ++frac_source_offset_;
      source_pos_modulo_ -= denominator_;
    }
    return frac_source_offset_;
  }

  // Advances to the end of this source and destination combo, returning the integer source frames
  // advanced. Skips as much source and destination frames as possible, returning the number of
  // whole source frames skipped.
  int64_t AdvanceToEnd();

  // Writes back the final offset values.
  void UpdateOffsets();

  // Returns source frame offset.
  Fixed source_offset() const { return Fixed::FromRaw(frac_source_offset_); }

  // Returns destination frame offset.
  int64_t dest_offset() const { return dest_offset_; }

  // Returns source position modulo.
  uint64_t source_pos_modulo() const { return source_pos_modulo_; }

 private:
  static void CheckDestPositions(int64_t dest_frame_count, int64_t dest_offset);
  static void CheckSourcePositions(int64_t source_frame_count, int64_t frac_source_offset,
                                   int64_t frac_pos_filter_length);
  static void CheckRateValues(int64_t frac_step_size, uint64_t rate_modulo, uint64_t denominator,
                              uint64_t source_pos_modulo);

  int32_t source_channel_count_ = 1;
  int32_t dest_channel_count_ = 1;
  int64_t frac_positive_length_ = 1;
  int64_t frac_negative_length_ = kFracOneFrame;

  void* source_void_ptr_ = nullptr;
  int64_t source_frame_count_ = 0;
  Fixed* source_offset_ptr_ = nullptr;
  int64_t frac_source_offset_ = 0;
  int64_t frac_source_end_ = 0;

  float* dest_ptr_ = nullptr;
  int64_t dest_frame_count_ = 0;
  int64_t* dest_offset_ptr_ = nullptr;
  int64_t dest_offset_ = 0;

  // If `SetRateValues` is never called, we successfully operate at 1:1 (without rate change).
  int64_t frac_step_size_ = kFracOneFrame;
  uint64_t rate_modulo_ = 0;
  uint64_t denominator_ = 1;
  uint64_t source_pos_modulo_ = 0;  // This should always be less than `rate_modulo_` (or both 0).
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_LIB_PROCESSING_POSITION_MANAGER_H_
