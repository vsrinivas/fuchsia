// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_CLOCK_REFERENCE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_CLOCK_REFERENCE_H_

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

#include "src/media/audio/lib/clock/utils.h"

namespace media::audio {

// In addition to being copyable, ClockReference abstracts clock rate-adjustment. An _adjustable_
// ClockReference allows its clock to be rate-adjusted; a _readonly_ ClockReference does not.
class ClockReference {
 public:
  static ClockReference MakeAdjustable(zx::clock& clock) { return ClockReference(&clock, true); }
  static ClockReference MakeReadonly(zx::clock& clock) { return ClockReference(&clock, false); }

  ClockReference() : ClockReference(nullptr, false) {}
  ClockReference(const ClockReference&) = default;
  ClockReference& operator=(const ClockReference&) = default;

  zx::time Read() const {
    FX_DCHECK(clock_) << "Null clock ref cannot be read";
    FX_DCHECK(clock_->is_valid()) << "Invalid clock ref cannot be read";
    zx::time now;
    auto status = clock_->read(now.get_address());
    FX_DCHECK(status == ZX_OK) << "Error while reading clock: " << status;
    return now;
  }

  const zx::clock& get() const {
    FX_DCHECK(clock_) << "Cannot get null clock ref";
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
  ClockReference(const zx::clock* clock, bool adjustable) : clock_(clock), adjustable_(adjustable) {
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

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_CLOCK_REFERENCE_H_
