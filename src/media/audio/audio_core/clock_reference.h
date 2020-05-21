// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_CLOCK_REFERENCE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_CLOCK_REFERENCE_H_

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

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

 private:
  ClockReference(const zx::clock* clock, bool adjustable)
      : clock_(clock), adjustable_(adjustable) {}

  const zx::clock* clock_;
  bool adjustable_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_CLOCK_REFERENCE_H_
