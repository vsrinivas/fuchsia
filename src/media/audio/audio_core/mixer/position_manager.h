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
  PositionManager(uint32_t num_src_chans, uint32_t num_dest_chans, uint32_t positive_width,
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

  // Establish the parameters for this source and dest
  void SetSourceValues(const void* src_void, uint32_t frac_src_frames, int32_t* frac_src_offset);
  void SetDestValues(float* dest, uint32_t dest_frames, uint32_t* dest_offset);
  // Specify the rate parameters. If not called, a unity rate (1:1) is assumed.
  void SetRateValues(uint32_t step_size, uint64_t rate_modulo, uint64_t denominator,
                     uint64_t* src_pos_mod);

  // Convenience method to retrieve the pointer to the first available source frame in this buffer.
  template <typename SrcSampleType>
  inline SrcSampleType* FirstSourceFrame() const;

  // Convenience method to retrieve the pointer to the last available source frame in this buffer.
  template <typename SrcSampleType>
  inline SrcSampleType* LastSourceFrame() const;

  // Retrieve the pointer to the current source frame (based on source offset)).
  template <typename SrcSampleType>
  inline SrcSampleType* CurrentSourceFrame() const;

  // Retrieve the pointer to the current destination frame (based on destination offset)).
  float* CurrentDestFrame() const { return dest_ + (dest_offset_ * num_dest_chans_); }

  // Convenience method related to whether previously-cached data should be referenced.
  bool SourcePositionIsBeforeBuffer() const { return frac_src_offset_ < 0; }

  // Is there enough remaining source data and destination space, to produce another frame?
  bool FrameCanBeMixed() const {
    return (dest_offset_ < dest_frames_) && (frac_src_offset_ <= frac_src_end_);
  }

  // Advance one dest frame (and related frac_src, incl modulo); return the new frac_src_offset.
  template <bool UseModulo = true>
  inline int32_t AdvanceFrame();

  // Skip as much src and dest as possible, returning the number of whole source frames skipped.
  // Not necessarily required to be inlined, as this is only invoked once per Mix() call.
  template <bool UseModulo = true>
  uint32_t AdvanceToEnd();

  // Write back the final offset values
  // Not necessarily required to be inlined, as this is only invoked once per Mix() call.
  void UpdateOffsets() {
    *frac_src_offset_ptr_ = frac_src_offset_;
    *dest_offset_ptr_ = dest_offset_;
    *src_pos_modulo_ptr_ = src_pos_modulo_;
  }

  // Is there NOT enough remaining source data to produce another output frame?
  bool SourceIsConsumed() const { return (frac_src_offset_ > frac_src_end_); }

  int32_t frac_src_offset() const { return frac_src_offset_; }
  uint32_t dest_offset() const { return dest_offset_; }

 private:
  uint32_t num_src_chans_;
  uint32_t num_dest_chans_;
  uint32_t positive_width_;
  uint32_t negative_width_;
  uint32_t frac_bits_;
  uint32_t frac_size_;
  uint32_t frac_mask_;
  uint32_t min_frac_src_frames_;

  void* src_void_ = nullptr;
  uint32_t frac_src_frames_ = 0;
  int32_t* frac_src_offset_ptr_ = nullptr;
  int32_t frac_src_offset_ = 0;
  int32_t frac_src_end_ = 0;
  // TODO(fxbug.dev/37356): Make frac_src_frames and frac_src_offset typesafe

  float* dest_ = nullptr;
  uint32_t dest_frames_ = 0;
  uint32_t* dest_offset_ptr_ = nullptr;
  uint32_t dest_offset_ = 0;

  // If SetRateValues is never called, we successfully operate at 1:1 (without rate change).
  bool using_modulo_ = false;
  uint32_t step_size_ = Mixer::FRAC_ONE;
  uint64_t rate_modulo_ = 0;
  uint64_t denominator_ = 1;
  uint64_t src_pos_modulo_ = 0;
  uint64_t* src_pos_modulo_ptr_ = &src_pos_modulo_;
};

//
// Inline implementations that must be in the .h because they are templated
template <typename SrcSampleType>
inline SrcSampleType* PositionManager::FirstSourceFrame() const {
  return static_cast<SrcSampleType*>(src_void_);
}

template <typename SrcSampleType>
inline SrcSampleType* PositionManager::LastSourceFrame() const {
  return static_cast<SrcSampleType*>(src_void_) +
         (((frac_src_frames_ - 1) >> media::audio::kPtsFractionalBits) * num_src_chans_);
}

template <typename SrcSampleType>
inline SrcSampleType* PositionManager::CurrentSourceFrame() const {
  FX_DCHECK(frac_src_offset_ >= 0);

  auto src = static_cast<SrcSampleType*>(src_void_);
  return src + ((frac_src_offset_ >> media::audio::kPtsFractionalBits) * num_src_chans_);
}

template <bool UseModulo>
inline int32_t PositionManager::AdvanceFrame() {
  ++dest_offset_;
  frac_src_offset_ += step_size_;

  if constexpr (UseModulo) {
    src_pos_modulo_ += rate_modulo_;
    if (src_pos_modulo_ >= denominator_) {
      ++frac_src_offset_;
      src_pos_modulo_ -= denominator_;
    }
  }
  return frac_src_offset_;
}

}  // namespace media::audio::mixer

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_POSITION_MANAGER_H_
