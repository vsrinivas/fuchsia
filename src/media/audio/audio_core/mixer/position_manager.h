// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_POSITION_MANAGER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_POSITION_MANAGER_H_

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include "src/media/audio/audio_core/mixer/constants.h"
#include "src/media/audio/audio_core/mixer/mixer.h"

namespace media::audio::mixer {

// PositionManager handles the updating of source and destination positions, as a resampler steps
// through source buffers with a specific step_size (based on the resampling ratio). This class
// extracts a significant amount of duplicate code across the resamplers.
class PositionManager {
 public:
  PositionManager() : PositionManager(1, 1, 1, kOneFrame.raw_value()) {}

  PositionManager(int32_t num_source_chans, int32_t num_dest_chans, int64_t positive_length,
                  int64_t negative_length);
  PositionManager(const PositionManager& not_ctor_copyable) = delete;
  PositionManager& operator=(const PositionManager& not_copyable) = delete;
  PositionManager(PositionManager&& not_ctor_movable) = delete;
  PositionManager& operator=(PositionManager&& not_movable) = delete;

  // Used for debugging purposes only
  static void Display(const PositionManager& pos_mgr);
  static void DisplayUpdate(const PositionManager& pos_mgr);

  // Establish the parameters for this source and dest
  void SetSourceValues(const void* source_void_ptr, int64_t source_frames,
                       Fixed* source_offset_ptr);
  void SetDestValues(float* dest_ptr, int64_t dest_frames, int64_t* dest_offset_ptr);
  // Specify the rate parameters. If not called, a unity rate (1:1) is assumed.
  void SetRateValues(int64_t step_size, uint64_t rate_modulo, uint64_t denominator,
                     uint64_t* source_pos_mod);

  static void CheckPositions(int64_t dest_frames, int64_t* dest_offset_ptr, int64_t source_frames,
                             int64_t source_offset, int64_t pos_filter_length,
                             Mixer::Bookkeeping* info);
  // Convenience method to retrieve the pointer to the first available source frame in this buffer.
  template <typename SourceSampleType>
  inline SourceSampleType* FirstSourceFrame() const;

  // Convenience method to retrieve the pointer to the last available source frame in this buffer.
  template <typename SourceSampleType>
  inline SourceSampleType* LastSourceFrame() const;

  // Retrieve the pointer to the current source frame (based on source offset)).
  template <typename SourceSampleType>
  inline SourceSampleType* CurrentSourceFrame() const;

  // Retrieve the pointer to the current destination frame (based on destination offset)).
  float* CurrentDestFrame() const { return dest_ptr_ + (dest_offset_ * num_dest_chans_); }

  // Convenience method related to whether previously-cached data should be referenced.
  bool SourcePositionIsBeforeBuffer() const { return frac_source_offset_ < 0; }

  // Is there enough remaining source data and destination space, to produce another frame?
  bool FrameCanBeMixed() const {
    return (dest_offset_ < dest_frames_) && (frac_source_offset_ < frac_source_end_);
  }

  // Advance one dest frame (and related source, incl modulo); return the new source_offset.
  inline int64_t AdvanceFrame();

  // Skip as much source and dest as possible, returning the number of whole source frames skipped.
  // Not necessarily required to be inlined, as this is only invoked once per Mix() call.
  int64_t AdvanceToEnd();

  // Write back the final offset values
  // Not necessarily required to be inlined, as this is only invoked once per Mix() call.
  void UpdateOffsets() {
    if (kMixerPositionTraceEvents) {
      TRACE_DURATION("audio", __func__, "source_offset",
                     Fixed::FromRaw(frac_source_offset_).Integral().Floor(), "source_offset.frac",
                     Fixed::FromRaw(frac_source_offset_).Fraction().raw_value(), "dest_offset",
                     dest_offset_, "source_pos_modulo", source_pos_modulo_);
    }
    *source_offset_ptr_ = Fixed::FromRaw(frac_source_offset_);
    *dest_offset_ptr_ = dest_offset_;
    if (rate_modulo_) {
      *source_pos_modulo_ptr_ = source_pos_modulo_;
    }
  }

  // Is there NOT enough remaining source data to produce another output frame?
  bool SourceIsConsumed() const { return (frac_source_offset_ >= frac_source_end_); }

  Fixed source_offset() const { return Fixed::FromRaw(frac_source_offset_); }
  int64_t dest_offset() const { return dest_offset_; }

 private:
  static void CheckDestPositions(int64_t dest_frames, int64_t* dest_offset_ptr);
  static void CheckSourcePositions(int64_t source_frames, int64_t frac_source_offset,
                                   int64_t frac_pos_filter_length);
  static void CheckRateValues(int64_t frac_step_size, uint64_t rate_modulo, uint64_t denominator,
                              uint64_t* source_position_modulo_ptr);

  int32_t num_source_chans_;
  int32_t num_dest_chans_;
  int64_t frac_positive_length_;
  int64_t frac_negative_length_;

  void* source_void_ptr_ = nullptr;
  int64_t source_frames_ = 0;
  Fixed* source_offset_ptr_ = nullptr;
  int64_t frac_source_offset_ = 0;
  int64_t frac_source_end_ = 0;

  float* dest_ptr_ = nullptr;
  int64_t dest_frames_ = 0;
  int64_t* dest_offset_ptr_ = nullptr;
  int64_t dest_offset_ = 0;

  // If SetRateValues is never called, we successfully operate at 1:1 (without rate change).
  int64_t frac_step_size_ = kOneFrame.raw_value();
  uint64_t rate_modulo_ = 0;
  uint64_t denominator_ = 1;
  uint64_t source_pos_modulo_ = 0;  // This should always be less than rate_modulo_ (or both 0).
  uint64_t* source_pos_modulo_ptr_ = &source_pos_modulo_;
};

//
// Inline implementations that must be in the .h because they are templated
template <typename SourceSampleType>
inline SourceSampleType* PositionManager::FirstSourceFrame() const {
  return static_cast<SourceSampleType*>(source_void_ptr_);
}

template <typename SourceSampleType>
inline SourceSampleType* PositionManager::LastSourceFrame() const {
  return static_cast<SourceSampleType*>(source_void_ptr_) +
         ((source_frames_ - 1) * num_source_chans_);
}

template <typename SourceSampleType>
inline SourceSampleType* PositionManager::CurrentSourceFrame() const {
  FX_CHECK(frac_source_offset_ >= 0);

  auto source_ptr = static_cast<SourceSampleType*>(source_void_ptr_);
  return source_ptr + (frac_source_offset_ >> Fixed::Format::FractionalBits) * num_source_chans_;
}

inline int64_t PositionManager::AdvanceFrame() {
  ++dest_offset_;
  frac_source_offset_ += frac_step_size_;

  source_pos_modulo_ += rate_modulo_;
  if (source_pos_modulo_ >= denominator_) {
    ++frac_source_offset_;
    source_pos_modulo_ -= denominator_;
  }
  return frac_source_offset_;
}

}  // namespace media::audio::mixer

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_POSITION_MANAGER_H_
