// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/position_manager.h"

#include "src/media/audio/lib/logging/logging.h"

namespace media::audio::mixer {

PositionManager::PositionManager(uint32_t num_source_chans, uint32_t num_dest_chans,
                                 uint32_t positive_width, uint32_t negative_width,
                                 uint32_t frac_bits)
    : num_source_chans_(num_source_chans),
      num_dest_chans_(num_dest_chans),
      positive_width_(positive_width),
      negative_width_(negative_width),
      frac_bits_(frac_bits),
      frac_size_(1u << frac_bits_),
      min_frac_source_frames_(positive_width_ + negative_width_ - (frac_size_ - 1)) {}

void PositionManager::Display(const PositionManager& pos_mgr, uint32_t frac_bits) {
  FX_LOGS(INFO) << "Channels: source " << pos_mgr.num_source_chans_ << ", dest "
                << pos_mgr.num_dest_chans_ << ".          Width: pos 0x" << std::hex
                << pos_mgr.positive_width_ << ", neg 0x" << pos_mgr.negative_width_;

  FX_LOGS(INFO) << "Source:   len 0x" << std::hex << pos_mgr.frac_source_frames_ << " (" << std::dec
                << (pos_mgr.frac_source_frames_ >> frac_bits) << "), end 0x" << std::hex
                << pos_mgr.frac_source_end_ << " (" << std::dec
                << (pos_mgr.frac_source_end_ >> frac_bits) << "), min_frames 0x" << std::hex
                << pos_mgr.min_frac_source_frames_ << ". Dest: len 0x" << pos_mgr.dest_frames_;

  FX_LOGS(INFO) << "Rate:     step_size 0x" << std::hex << pos_mgr.step_size_ << ", rate_mod "
                << std::dec << pos_mgr.rate_modulo_ << ", denom " << pos_mgr.denominator_
                << ", using_mod " << pos_mgr.using_modulo_;

  DisplayUpdate(pos_mgr, frac_bits);
}

void PositionManager::DisplayUpdate(const PositionManager& pos_mgr, uint32_t frac_bits) {
  const auto frac_mask = (1u << frac_bits) - 1u;
  FX_LOGS(INFO) << "Position: frac_source_offset " << std::hex
                << (pos_mgr.frac_source_offset_ < 0 ? "-" : " ") << "0x"
                << std::abs(pos_mgr.frac_source_offset_ >> frac_bits) << ":"
                << (pos_mgr.frac_source_offset_ & frac_mask) << ", dest_offset 0x"
                << pos_mgr.dest_offset_ << ", source_pos_mod 0x" << pos_mgr.source_pos_modulo_;
}

// static
void PositionManager::CheckPositions(uint32_t dest_frames, uint32_t* dest_offset_ptr,
                                     uint32_t frac_source_frames, int32_t* frac_source_offset_ptr,
                                     Fixed pos_filter_width, Mixer::Bookkeeping* info) {
  CheckDestPositions(dest_frames, dest_offset_ptr);

  CheckSourcePositions(frac_source_frames, frac_source_offset_ptr, pos_filter_width);

  CheckRateValues(info->step_size, info->rate_modulo(), info->denominator(),
                  &info->source_pos_modulo);
}

void PositionManager::SetSourceValues(const void* source_void_ptr, uint32_t frac_source_frames,
                                      int32_t* frac_source_offset_ptr) {
  CheckSourcePositions(frac_source_frames, frac_source_offset_ptr, Fixed::FromRaw(positive_width_));

  source_void_ptr_ = const_cast<void*>(source_void_ptr);

  frac_source_frames_ = frac_source_frames;
  frac_source_offset_ptr_ = frac_source_offset_ptr;
  frac_source_offset_ = *frac_source_offset_ptr_;

  // The last subframe for which this Mix call can produce output. Because of filter width, output
  // for the subsequent position requires source frames that we have not yet received.
  frac_source_end_ = static_cast<int32_t>(frac_source_frames_ - positive_width_) - 1;
}

// static
void PositionManager::CheckSourcePositions(uint32_t frac_source_frames,
                                           int32_t* frac_source_offset_ptr,
                                           Fixed pos_filter_width) {
  // Interp offset is an int32. frac_source_frames is uint32, but callers cannot exceed
  // int32_t::max()
  FX_CHECK(frac_source_frames <= std::numeric_limits<int32_t>::max())
      << "frac_source_frames (0x" << std::hex << frac_source_frames << ") too large, must be "
      << std::numeric_limits<int32_t>::max() << " or less.";

  // number-of-source-frames is fixed-point (aligns w/ frac_source_offset) but is always integral.
  // Filter width must be non-negative, and the source data provided must exceed filter_width.
  FX_CHECK((frac_source_frames & Mixer::FRAC_MASK) == 0)
      << "frac_source_frames (0x" << std::hex << frac_source_frames
      << ") should have fraction of 0 (is 0x" << (frac_source_frames & Mixer::FRAC_MASK) << ")";
  FX_CHECK(static_cast<int64_t>(frac_source_frames) >= Fixed(1).raw_value())
      << "Insufficient source buffer size (0x" << std::hex << frac_source_frames
      << ", must be at least one frame 0x" << Fixed(1).raw_value() << ")";

  auto frac_source_offset = *frac_source_offset_ptr;
  // "Source offset" can be negative but only within bounds of pos_filter_width.
  FX_CHECK(pos_filter_width >= 0) << "Mixer lookahead pos_filter_width (0x" << std::hex
                                  << pos_filter_width.raw_value() << ") cannot be negative";
  FX_CHECK(frac_source_offset + pos_filter_width.raw_value() >= 0)
      << "frac_source_offset (0x" << std::hex << frac_source_offset << ") plus pos_filter_width (0x"
      << pos_filter_width.raw_value() << ") must reach zero";

  // Source_offset cannot exceed frac_source_frames.
  FX_CHECK(frac_source_offset <= static_cast<int32_t>(frac_source_frames))
      << std::hex << "frac_source_offset 0x" << frac_source_offset
      << " cannot exceed frac_source_frames: 0x" << frac_source_frames;
  // Range (frac_source_frames-pos_filter_width,frac_source_frames) is allowed. A mixer should
  // produce no output while "priming" so it can subsequently start at frac_source_offset 0 with a
  // full cache.
}

void PositionManager::SetDestValues(float* dest_ptr, uint32_t dest_frames,
                                    uint32_t* dest_offset_ptr) {
  CheckDestPositions(dest_frames, dest_offset_ptr);

  dest_ptr_ = dest_ptr;
  dest_frames_ = dest_frames;
  dest_offset_ptr_ = dest_offset_ptr;
  dest_offset_ = *dest_offset_ptr_;
}

// static
void PositionManager::CheckDestPositions(uint32_t dest_frames, uint32_t* dest_offset_ptr) {
  // Location of first dest frame to produce must be within the provided buffer.
  FX_CHECK(*dest_offset_ptr < dest_frames)
      << "dest_offset (" << *dest_offset_ptr << ") must be less than dest_frames (" << dest_frames
      << ")";
}

void PositionManager::SetRateValues(uint32_t step_size, uint64_t rate_modulo, uint64_t denominator,
                                    uint64_t* source_pos_mod) {
  CheckRateValues(step_size, rate_modulo, denominator, source_pos_mod);

  step_size_ = step_size;
  using_modulo_ = (rate_modulo > 0 && denominator > 0);

  if (using_modulo_) {
    denominator_ = denominator;
    rate_modulo_ = rate_modulo;
    source_pos_modulo_ptr_ = source_pos_mod;
    source_pos_modulo_ = *source_pos_modulo_ptr_;
  }
}

// static
void PositionManager::CheckRateValues(uint32_t step_size, uint64_t rate_modulo,
                                      uint64_t denominator, uint64_t* source_position_modulo_ptr) {
  FX_CHECK(step_size > 0) << "step_size must be positive; cannot be zero";

  auto source_position_modulo = *source_position_modulo_ptr;

  FX_CHECK((rate_modulo == 0) || (rate_modulo < denominator))
      << "rate_modulo (" << rate_modulo << ") must be less than denominator (" << denominator
      << "), or both must be zero (source_position_modulo " << source_position_modulo << ")";

  FX_CHECK((source_position_modulo == 0) || (source_position_modulo < denominator))
      << "source_position_modulo (" << source_position_modulo << ") must be less than denominator ("
      << denominator << "), or both must be zero (rate_modulo " << rate_modulo << ")";
}

template <bool UseModulo>
uint32_t PositionManager::AdvanceToEnd() {
  if (!FrameCanBeMixed()) {
    return 0;
  }

  // Number of source steps available, if no rate_modulo effect.
  const uint32_t source_rough_steps_avail =
      (frac_source_end_ - frac_source_offset_) / step_size_ + 1;
  const auto dest_frames_avail = dest_frames_ - dest_offset_;
  const auto avail = std::min(dest_frames_avail, source_rough_steps_avail);

  const auto prev_source_frame_consumed =
      (frac_source_offset_ + static_cast<int32_t>(positive_width_)) >> frac_bits_;

  frac_source_offset_ += (avail * step_size_);
  dest_offset_ += avail;

  if (UseModulo && using_modulo_) {
    const uint64_t total_mod = source_pos_modulo_ + (avail * rate_modulo_);
    frac_source_offset_ += (total_mod / denominator_);
    source_pos_modulo_ = total_mod % denominator_;

    int32_t prev_source_offset = (source_pos_modulo_ < rate_modulo_)
                                     ? (frac_source_offset_ - step_size_ - 1)
                                     : (frac_source_offset_ - step_size_);
    while (prev_source_offset > frac_source_end_) {
      if (source_pos_modulo_ < rate_modulo_) {
        source_pos_modulo_ += denominator_;
      }
      source_pos_modulo_ -= rate_modulo_;

      --dest_offset_;
      frac_source_offset_ = prev_source_offset;

      prev_source_offset = (source_pos_modulo_ < rate_modulo_)
                               ? (frac_source_offset_ - step_size_ - 1)
                               : (frac_source_offset_ - step_size_);
    }
  }

  const auto new_source_frame_consumed =
      (frac_source_offset_ + static_cast<int32_t>(positive_width_)) >> frac_bits_;
  return new_source_frame_consumed - prev_source_frame_consumed;
}

template uint32_t PositionManager::AdvanceToEnd<true>();
template uint32_t PositionManager::AdvanceToEnd<false>();

}  // namespace media::audio::mixer
