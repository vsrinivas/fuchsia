// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CLOCK_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CLOCK_H_

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

#include "src/media/audio/lib/clock/utils.h"

namespace media::audio {

// In addition to being copyable, AudioClock abstracts clock rate-adjustment. An _adjustable_
// AudioClock allows its clock to be rate-adjusted; a _readonly_ AudioClock does not.
class AudioClock {
 public:
  static AudioClock MakeAdjustable(zx::clock& clock) { return AudioClock(&clock, true); }
  static AudioClock MakeReadonly(zx::clock& clock) { return AudioClock(&clock, false); }

  AudioClock() : AudioClock(nullptr, false) {}
  AudioClock(const AudioClock&) = default;
  AudioClock& operator=(const AudioClock&) = default;

  zx::time Read() const {
    FX_DCHECK(clock_) << "Null clock cannot be read";
    FX_DCHECK(clock_->is_valid()) << "Invalid clock cannot be read";
    zx::time now;
    auto status = clock_->read(now.get_address());
    FX_DCHECK(status == ZX_OK) << "Error while reading clock: " << status;
    return now;
  }

  const zx::clock& get() const {
    FX_DCHECK(clock_) << "Cannot get null clock";
    return *clock_;
  }

  bool is_valid() const { return (clock_ != nullptr && clock_->is_valid()); }
  explicit operator bool() const { return is_valid(); }
  bool adjustable() const { return adjustable_; }

  const TimelineFunction& ref_clock_to_clock_mono() {
    FX_DCHECK(clock_) << "this (" << std::hex << static_cast<void*>(this)
                      << "), ref_clock_to_clock_mono called before clock_ was established";
    FX_DCHECK(clock_->is_valid()) << "this (" << std::hex << static_cast<void*>(this)
                                  << "), ref_clock_to_clock_mono called before clock_ was valid";

    auto result = audio::clock::SnapshotClock(*clock_);
    FX_DCHECK(result.is_ok()) << "SnapshotClock failed";
    ref_clock_to_clock_mono_ = result.take_value().reference_to_monotonic;

    return ref_clock_to_clock_mono_;
  }
  const TimelineFunction& quick_ref_clock_to_clock_mono() { return ref_clock_to_clock_mono_; }

 private:
  AudioClock(const zx::clock* clock, bool adjustable) : clock_(clock), adjustable_(adjustable) {
    if (clock_ && clock_->is_valid()) {
      auto result = audio::clock::SnapshotClock(*clock_);
      FX_DCHECK(result.is_ok()) << "SnapshotClock failed";
      ref_clock_to_clock_mono_ = result.take_value().reference_to_monotonic;
    }
  }

  const zx::clock* clock_;
  bool adjustable_;
  TimelineFunction ref_clock_to_clock_mono_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CLOCK_H_
