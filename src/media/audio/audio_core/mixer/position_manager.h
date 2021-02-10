// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_POSITION_MANAGER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_POSITION_MANAGER_H_

#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/audio_core/mixer/constants.h"
#include "src/media/audio/audio_core/mixer/mixer.h"

namespace media::audio::mixer {

// PositionManager handles the updating of source and destination positions, as a resampler steps
// through source buffers with a specific step_size (based on the resampling ratio). This class
// extracts a significant amount of duplicate code across the resamplers.
class PositionManager {
 public:
  PositionManager() : PositionManager(1, 1, 0, Mixer::FRAC_ONE - 1){};
  PositionManager(uint32_t num_source_chans, uint32_t num_dest_chans, uint32_t positive_width,
                  uint32_t negative_width, uint32_t frac_bits = media::audio::kPtsFractionalBits);
  PositionManager(const PositionManager& not_ctor_copyable) = delete;
  PositionManager& operator=(const PositionManager& not_copyable) = delete;
  PositionManager(PositionManager&& not_ctor_movable) = delete;
  PositionManager& operator=(PositionManager&& not_movable) = delete;

  // Used for debugging purposes only
  static void Display(const PositionManager& pos_mgr,
                      uint32_t frac_bits = media::audio::kPtsFractionalBits);
  static void DisplayUpdate(const PositionManager& pos_mgr,
                            uint32_t frac_bits = media::audio::kPtsFractionalBits);

  static void CheckPositions(uint32_t dest_frames, uint32_t* dest_offset_ptr,
                             uint32_t frac_source_frames, int32_t* frac_source_offset_ptr,
                             Fixed pos_filter_width, Mixer::Bookkeeping* info);

  // Establish the parameters for this source and dest
  void SetSourceValues(const void* source_void_ptr, uint32_t frac_source_frames,
                       int32_t* frac_source_offset_ptr);
  void SetDestValues(float* dest_ptr, uint32_t dest_frames, uint32_t* dest_offset_ptr);
  // Specify the rate parameters. If not called, a unity rate (1:1) is assumed.
  void SetRateValues(uint32_t step_size, uint64_t rate_modulo, uint64_t denominator,
                     uint64_t* source_pos_mod);

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
    return (dest_offset_ < dest_frames_) && (frac_source_offset_ <= frac_source_end_);
  }

  // Advance one dest frame (and frac_source, incl modulo); return the new frac_source_offset.
  template <bool UseModulo = true>
  inline int32_t AdvanceFrame();

  // Skip as much source and dest as possible, returning the number of whole source frames skipped.
  // Not necessarily required to be inlined, as this is only invoked once per Mix() call.
  template <bool UseModulo = true>
  uint32_t AdvanceToEnd();

  // Write back the final offset values
  // Not necessarily required to be inlined, as this is only invoked once per Mix() call.
  void UpdateOffsets() {
    *frac_source_offset_ptr_ = frac_source_offset_;
    *dest_offset_ptr_ = dest_offset_;
    if (using_modulo_) {
      *source_pos_modulo_ptr_ = source_pos_modulo_;
    }
  }

  // Is there NOT enough remaining source data to produce another output frame?
  bool SourceIsConsumed() const { return (frac_source_offset_ > frac_source_end_); }

  int32_t frac_source_offset() const { return frac_source_offset_; }
  uint32_t dest_offset() const { return dest_offset_; }

 private:
  static inline void CheckSourcePositions(uint32_t frac_source_frames,
                                          int32_t* frac_source_offset_ptr, Fixed pos_filter_width);
  static inline void CheckDestPositions(uint32_t dest_frames, uint32_t* dest_offset_ptr);
  static inline void CheckRateValues(uint32_t step_size, uint64_t rate_modulo, uint64_t denominator,
                                     uint64_t* source_position_modulo_ptr);

  const uint32_t num_source_chans_;
  const uint32_t num_dest_chans_;
  const uint32_t positive_width_;
  const uint32_t negative_width_;
  const uint32_t frac_bits_;
  const uint32_t frac_size_;
  const uint32_t min_frac_source_frames_;

  void* source_void_ptr_ = nullptr;
  // TODO(fxbug.dev/37356): Make frac_source_frames and frac_source_offset typesafe
  uint32_t frac_source_frames_ = 0;
  int32_t* frac_source_offset_ptr_ = nullptr;
  int32_t frac_source_offset_ = 0;
  int32_t frac_source_end_ = 0;  // the last sampleable fractional frame of this source region

  float* dest_ptr_ = nullptr;
  uint32_t dest_frames_ = 0;
  uint32_t* dest_offset_ptr_ = nullptr;
  uint32_t dest_offset_ = 0;

  // If SetRateValues is never called, we successfully operate at 1:1 (without rate change).
  bool using_modulo_ = false;
  uint32_t step_size_ = Mixer::FRAC_ONE;
  uint64_t rate_modulo_ = 0;
  uint64_t denominator_ = 1;
  uint64_t source_pos_modulo_ = 0;
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
         (((frac_source_frames_ - 1) >> media::audio::kPtsFractionalBits) * num_source_chans_);
}

template <typename SourceSampleType>
inline SourceSampleType* PositionManager::CurrentSourceFrame() const {
  FX_DCHECK(frac_source_offset_ >= 0);

  auto source = static_cast<SourceSampleType*>(source_void_ptr_);
  return source + ((frac_source_offset_ >> media::audio::kPtsFractionalBits) * num_source_chans_);
}

template <bool UseModulo>
inline int32_t PositionManager::AdvanceFrame() {
  ++dest_offset_;
  frac_source_offset_ += step_size_;

  if (UseModulo && using_modulo_) {
    source_pos_modulo_ += rate_modulo_;
    if (source_pos_modulo_ >= denominator_) {
      ++frac_source_offset_;
      source_pos_modulo_ -= denominator_;
    }
  }
  return frac_source_offset_;
}

}  // namespace media::audio::mixer

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_POSITION_MANAGER_H_
