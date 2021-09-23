// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/position_manager.h"

#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/audio_core/mixer/mixer.h"

namespace media::audio::mixer {

PositionManager::PositionManager(int32_t num_source_chans, int32_t num_dest_chans,
                                 int64_t frac_positive_length, int64_t frac_negative_length)
    : num_source_chans_(num_source_chans),
      num_dest_chans_(num_dest_chans),
      frac_positive_length_(frac_positive_length),
      frac_negative_length_(frac_negative_length) {
  FX_CHECK(frac_positive_length_ > 0);
  FX_CHECK(frac_negative_length_ > 0);
}

void PositionManager::Display(const PositionManager& pos_mgr) {
  FX_LOGS(INFO) << "Channels: source " << pos_mgr.num_source_chans_ << ", dest "
                << pos_mgr.num_dest_chans_ << ".   Filter Length: pos " << ffl::String::DecRational
                << Fixed::FromRaw(pos_mgr.frac_positive_length_) << ", neg "
                << Fixed::FromRaw(pos_mgr.frac_negative_length_);

  FX_LOGS(INFO) << "Source:   len " << pos_mgr.source_frames_ << ", to " << ffl::String::DecRational
                << Fixed::FromRaw(pos_mgr.frac_source_end_) << ". Dest: len "
                << pos_mgr.dest_frames_;

  FX_LOGS(INFO) << "Rate:     frac_step_size " << ffl::String::DecRational
                << Fixed(pos_mgr.frac_step_size_) << ", rate_mod " << pos_mgr.rate_modulo_
                << ", denom " << pos_mgr.denominator_;

  DisplayUpdate(pos_mgr);
}

void PositionManager::DisplayUpdate(const PositionManager& pos_mgr) {
  FX_LOGS(INFO) << "Position: frac_source_offset " << ffl::String::DecRational
                << Fixed::FromRaw(pos_mgr.frac_source_offset_) << ": dest_offset "
                << pos_mgr.dest_offset_ << ", pos_mod " << pos_mgr.source_pos_modulo_;
}

// static
void PositionManager::CheckPositions(int64_t dest_frames, int64_t* dest_offset_ptr,
                                     int64_t source_frames, int64_t frac_source_offset,
                                     int64_t frac_pos_filter_length, Mixer::Bookkeeping* info) {
  CheckDestPositions(dest_frames, dest_offset_ptr);

  CheckSourcePositions(source_frames, frac_source_offset, frac_pos_filter_length);

  CheckRateValues(info->step_size.raw_value(), info->rate_modulo(), info->denominator(),
                  &info->source_pos_modulo);
}

void PositionManager::SetDestValues(float* dest_ptr, int64_t dest_frames,
                                    int64_t* dest_offset_ptr) {
  if (kMixerPositionTraceEvents) {
    TRACE_DURATION("audio", __func__, "dest_frames", dest_frames, "dest_offset", *dest_offset_ptr);
  }
  CheckDestPositions(dest_frames, dest_offset_ptr);

  dest_ptr_ = dest_ptr;
  dest_frames_ = dest_frames;
  dest_offset_ptr_ = dest_offset_ptr;
  dest_offset_ = *dest_offset_ptr_;
}

// static
void PositionManager::CheckDestPositions(int64_t dest_frames, int64_t* dest_offset_ptr) {
  // Location of first dest frame cannot be negative.
  FX_CHECK(*dest_offset_ptr >= 0) << "dest_offset (" << *dest_offset_ptr
                                  << ") must be non-negative";

  // Location of first dest frame to produce must be within the provided buffer.
  FX_CHECK(*dest_offset_ptr < dest_frames)
      << "dest_offset (" << *dest_offset_ptr << ") must be less than dest_frames (" << dest_frames
      << ")";
}

void PositionManager::SetSourceValues(const void* source_void_ptr, int64_t source_frames,
                                      Fixed* source_offset_ptr) {
  if (kMixerPositionTraceEvents) {
    TRACE_DURATION("audio", __func__, "source_frames", source_frames, "source_offset",
                   source_offset_ptr->Integral().Floor(), "source_offset.frac",
                   source_offset_ptr->Fraction().raw_value());
  }
  source_void_ptr_ = const_cast<void*>(source_void_ptr);
  source_frames_ = source_frames;
  source_offset_ptr_ = source_offset_ptr;
  frac_source_offset_ = source_offset_ptr_->raw_value();

  // frac_source_end_ is the first subframe at which this Mix call can no longer produce output
  frac_source_end_ = (source_frames << Fixed::Format::FractionalBits) - frac_positive_length_ + 1;
}

// static
void PositionManager::CheckSourcePositions(int64_t source_frames, int64_t frac_source_offset,
                                           int64_t frac_pos_filter_length) {
  FX_CHECK(source_frames > 0) << "Source buffer must have at least one frame";
  FX_CHECK(frac_pos_filter_length > 0)
      << "Mixer lookahead frac_pos_filter_length (" << ffl::String::DecRational
      << Fixed::FromRaw(frac_pos_filter_length) << ") must be positive";

  // "Source offset" can be negative but only within bounds of frac_pos_filter_length.
  FX_CHECK(frac_source_offset + frac_pos_filter_length > 0)
      << "frac_source_offset (" << ffl::String::DecRational << Fixed::FromRaw(frac_source_offset)
      << ") must be greater than -pos_length (" << Fixed::FromRaw(-frac_pos_filter_length) << ")";

  // Source_offset cannot exceed source_frames.
  FX_CHECK(((frac_source_offset - 1) >> Fixed::Format::FractionalBits) < source_frames)
      << "frac_source_offset: " << ffl::String::DecRational << Fixed::FromRaw(frac_source_offset)
      << ", source_frames_count: " << source_frames;
}

void PositionManager::SetRateValues(int64_t frac_step_size, uint64_t rate_modulo,
                                    uint64_t denominator, uint64_t* source_pos_mod) {
  if (kMixerPositionTraceEvents) {
    TRACE_DURATION("audio", __func__, "step_size",
                   Fixed::FromRaw(frac_step_size).Integral().Floor(), "step_size.frac",
                   Fixed::FromRaw(frac_step_size).Fraction().raw_value(), "rate_modulo",
                   rate_modulo, "denominator", denominator);
  }
  CheckRateValues(frac_step_size, rate_modulo, denominator, source_pos_mod);

  frac_step_size_ = frac_step_size;
  rate_modulo_ = rate_modulo;

  if (rate_modulo_ > 0) {
    denominator_ = denominator;
    source_pos_modulo_ptr_ = source_pos_mod;
    source_pos_modulo_ = *source_pos_modulo_ptr_;
  }
}

// static
void PositionManager::CheckRateValues(int64_t frac_step_size, uint64_t rate_modulo,
                                      uint64_t denominator, uint64_t* source_position_modulo_ptr) {
  FX_CHECK(frac_step_size > 0) << "step_size must be positive; cannot be zero";

  FX_CHECK(denominator > 0) << "denominator cannot be zero";

  FX_CHECK(rate_modulo < denominator) << "rate_modulo (" << rate_modulo
                                      << ") must be less than denominator (" << denominator << ")";

  FX_CHECK(*source_position_modulo_ptr < denominator)
      << "source_position_modulo (" << *source_position_modulo_ptr
      << ") must be less than denominator (" << denominator << ")";
}

// Advance to the end of this source/dest combo, returning the integer source frames advanced.
int64_t PositionManager::AdvanceToEnd() {
  if (!FrameCanBeMixed()) {
    return 0;
  }

  // Number of source steps available, if no rate_modulo effect.
  const int64_t est_dest_frames_produced =
      (frac_source_end_ - frac_source_offset_ - 1) / frac_step_size_ + 1;
  const int64_t dest_frames_space_avail = dest_frames_ - dest_offset_;
  const auto avail = std::min(est_dest_frames_produced, dest_frames_space_avail);

  const auto prev_source_frame_consumed =
      (frac_source_offset_ + frac_positive_length_ - 1) >> Fixed::Format::FractionalBits;

  // Advance source and dest by 'avail' steps.
  frac_source_offset_ += (avail * frac_step_size_);
  dest_offset_ += avail;

  if (rate_modulo_) {
    // Compute the modulo after advancing, and increment frac_source_offset_ accordingly.
    const uint64_t total_mod = source_pos_modulo_ + (avail * rate_modulo_);
    frac_source_offset_ += total_mod / denominator_;
    source_pos_modulo_ = total_mod % denominator_;

    // Maintain an offset of previous source, for the last dest frame we would produce.
    int64_t prev_source_offset = frac_source_offset_ - frac_step_size_;
    // If source_pos_modulo_ JUST rolled over, then decrement prev_source_offset_
    prev_source_offset -= (source_pos_modulo_ < rate_modulo_) ? 1 : 0;

    // If the rough estimate advanced position too far, roll position back until it is correct.
    // For the final dest frame we produce, prev_source_offset must be less than frac_source_end_.
    while (prev_source_offset >= frac_source_end_) {
      source_pos_modulo_ += ((source_pos_modulo_ < rate_modulo_) ? denominator_ : 0);
      source_pos_modulo_ -= rate_modulo_;

      --dest_offset_;
      frac_source_offset_ = prev_source_offset;

      prev_source_offset = frac_source_offset_ - frac_step_size_;
      // If source_pos_modulo_ rolled over for frac_source_offset_, decrement prev_source_offset_
      prev_source_offset -= (source_pos_modulo_ < rate_modulo_) ? 1 : 0;
    }
  }

  const auto new_source_frame_consumed =
      (frac_source_offset_ + frac_positive_length_ - 1) >> Fixed::Format::FractionalBits;
  return new_source_frame_consumed - prev_source_frame_consumed;
}

}  // namespace media::audio::mixer
