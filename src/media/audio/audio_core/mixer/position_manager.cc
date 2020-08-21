// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/position_manager.h"

#include "src/media/audio/lib/logging/logging.h"

namespace media::audio::mixer {

PositionManager::PositionManager(uint32_t num_src_chans, uint32_t num_dest_chans,
                                 uint32_t positive_width, uint32_t negative_width,
                                 uint32_t frac_bits)
    : num_src_chans_(num_src_chans),
      num_dest_chans_(num_dest_chans),
      positive_width_(positive_width),
      negative_width_(negative_width),
      frac_bits_(frac_bits) {
  frac_size_ = 1u << frac_bits_;
  frac_mask_ = frac_size_ - 1;
  min_frac_src_frames_ = positive_width_ + negative_width_ - (frac_size_ - 1);
}

void PositionManager::Display(const PositionManager& pos_mgr, uint32_t frac_bits) {
  FX_LOGS(TRACE) << "Channels: src " << pos_mgr.num_src_chans_ << ", dest "
                 << pos_mgr.num_dest_chans_ << ".          Width: pos 0x" << std::hex
                 << pos_mgr.positive_width_ << ", neg 0x" << pos_mgr.negative_width_;

  FX_LOGS(TRACE) << "Source:   len 0x" << std::hex << pos_mgr.frac_src_frames_ << " (" << std::dec
                 << (pos_mgr.frac_src_frames_ >> frac_bits) << "), end 0x" << std::hex
                 << pos_mgr.frac_src_end_ << " (" << std::dec
                 << (pos_mgr.frac_src_end_ >> frac_bits) << "), min_frames 0x" << std::hex
                 << pos_mgr.min_frac_src_frames_ << ". Dest: len 0x" << pos_mgr.dest_frames_;

  FX_LOGS(TRACE) << "Rate:     step_size 0x" << std::hex << pos_mgr.step_size_ << ", rate_mod "
                 << std::dec << pos_mgr.rate_modulo_ << ", denom " << pos_mgr.denominator_
                 << ", using_mod " << pos_mgr.using_modulo_;

  DisplayUpdate(pos_mgr, frac_bits);
}

void PositionManager::DisplayUpdate(const PositionManager& pos_mgr, uint32_t frac_bits) {
  const auto frac_mask = (1u << frac_bits) - 1u;
  FX_LOGS(TRACE) << "Position: frac_src_offset " << std::hex
                 << (pos_mgr.frac_src_offset_ < 0 ? "-" : " ") << "0x"
                 << std::abs(pos_mgr.frac_src_offset_ >> frac_bits) << ":"
                 << (pos_mgr.frac_src_offset_ & frac_mask) << ", dest_offset 0x"
                 << pos_mgr.dest_offset_ << ", src_pos_mod 0x" << pos_mgr.src_pos_modulo_;
}

void PositionManager::SetSourceValues(const void* src_void, uint32_t frac_src_frames,
                                      int32_t* frac_src_offset) {
  src_void_ = const_cast<void*>(src_void);

  frac_src_frames_ = frac_src_frames;

  // We express number-of-source-frames as fixed-point 19.13 (to align with frac_src_offset) but the
  // actual number of frames provided is always an integer.
  FX_DCHECK((frac_src_frames_ & frac_mask_) == 0);

  // Interp offset is an int32. frac_src_frames is uint32, but callers cannot exceed int32_t::max()
  FX_DCHECK(frac_src_frames_ <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));

  // The source buffer must provide us at least one frame
  FX_DCHECK(frac_src_frames_ >= frac_size_);

  frac_src_offset_ptr_ = frac_src_offset;
  frac_src_offset_ = *frac_src_offset_ptr_;

  // "Source offset" can be negative within bounds of pos_filter_width. Callers must ensure this.
  FX_DCHECK(frac_src_offset_ + positive_width_ >= 0)
      << std::hex << "frac_src_off: 0x" << frac_src_offset_;

  // frac_src_offset_ cannot exceed our last sampleable subframe. We define this as "Source end":
  // the last subframe for which this Mix call can produce output. Otherwise, these src samples are
  // in the past: they may impact future output but are insufficient for us to produce output here.
  frac_src_end_ = static_cast<int32_t>(frac_src_frames_ - positive_width_) - 1;

  // Strictly, src_off should be LESS THAN frac_src_frames. We also allow them to be exactly equal,
  // as this is used to "prime" resamplers that use a significant amount of previously-cached data.
  // When equal, we produce no output frame, but samplers with history will cache the final frames.
  FX_DCHECK(frac_src_offset_ <= static_cast<int32_t>(frac_src_frames_))
      << std::hex << "frac_src_off: 0x" << frac_src_offset_ << ", frac_src_end: 0x" << frac_src_end_
      << ", frac_src_frames: 0x" << frac_src_frames_;
}

void PositionManager::SetDestValues(float* dest, uint32_t dest_frames, uint32_t* dest_offset) {
  dest_ = dest;
  dest_frames_ = dest_frames;
  dest_offset_ptr_ = dest_offset;
  dest_offset_ = *dest_offset_ptr_;

  // Location of first dest frame to produce must be within the provided buffer.
  FX_DCHECK(dest_offset_ < dest_frames_);
}

void PositionManager::SetRateValues(uint32_t step_size, uint64_t rate_modulo, uint64_t denominator,
                                    uint64_t* src_pos_mod) {
  step_size_ = step_size;
  FX_DCHECK(step_size > 0);

  rate_modulo_ = rate_modulo;
  src_pos_modulo_ptr_ = src_pos_mod;
  FX_DCHECK(src_pos_modulo_ptr_ != nullptr);
  src_pos_modulo_ = *src_pos_modulo_ptr_;
  using_modulo_ = (rate_modulo_ > 0);

  if (using_modulo_) {
    denominator_ = denominator;

    FX_DCHECK(denominator_ > 0);
    FX_DCHECK(denominator_ > rate_modulo_);
    FX_DCHECK(denominator_ > src_pos_modulo_);
  } else {
    denominator_ = src_pos_modulo_ + 1;  //  so rollover comparisons work as they should
  }
}

template <bool UseModulo>
uint32_t PositionManager::AdvanceToEnd() {
  if (!FrameCanBeMixed()) {
    return 0;
  }

  const uint32_t src_rough_steps_avail = (frac_src_end_ - frac_src_offset_) / step_size_ + 1;
  const auto dest_frames_avail = dest_frames_ - dest_offset_;
  const auto avail = std::min(dest_frames_avail, src_rough_steps_avail);

  const auto prev_src_frame_consumed =
      (frac_src_offset_ + static_cast<int32_t>(positive_width_)) >> frac_bits_;

  frac_src_offset_ += (avail * step_size_);
  dest_offset_ += avail;

  if constexpr (UseModulo) {
    if (using_modulo_) {
      const uint64_t total_mod = src_pos_modulo_ + (avail * rate_modulo_);
      frac_src_offset_ += (total_mod / denominator_);
      src_pos_modulo_ = total_mod % denominator_;

      int32_t prev_src_offset = (src_pos_modulo_ < rate_modulo_)
                                    ? (frac_src_offset_ - step_size_ - 1)
                                    : (frac_src_offset_ - step_size_);
      while (prev_src_offset > frac_src_end_) {
        if (src_pos_modulo_ < rate_modulo_) {
          src_pos_modulo_ += denominator_;
        }
        src_pos_modulo_ -= rate_modulo_;

        --dest_offset_;
        frac_src_offset_ = prev_src_offset;

        prev_src_offset = (src_pos_modulo_ < rate_modulo_) ? (frac_src_offset_ - step_size_ - 1)
                                                           : (frac_src_offset_ - step_size_);
      }
    }
  }

  const auto new_src_frame_consumed =
      (frac_src_offset_ + static_cast<int32_t>(positive_width_)) >> frac_bits_;
  return new_src_frame_consumed - prev_src_frame_consumed;
}

template uint32_t PositionManager::AdvanceToEnd<true>();
template uint32_t PositionManager::AdvanceToEnd<false>();

}  // namespace media::audio::mixer
