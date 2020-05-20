// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_CLOCK_REFERENCE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_CLOCK_REFERENCE_H_

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

namespace media::audio {

class ClockReference {
 public:
  static ClockReference MakeWritable(zx::clock& clock) { return ClockReference(&clock, true); }
  static ClockReference MakeReadonly(zx::clock& clock) { return ClockReference(&clock, false); }

  ClockReference() : ClockReference(nullptr, false) {}
  ClockReference(const ClockReference& copied) : ClockReference(copied.clock_, copied.writable_) {}
  ClockReference& operator=(const ClockReference& copied) {
    clock_ = copied.clock_;
    writable_ = copied.writable_;
    return *this;
  }

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
  bool writable() const { return writable_; }

 private:
  ClockReference(const zx::clock* clock, bool writable) : clock_(clock), writable_(writable) {}

  const zx::clock* clock_;
  bool writable_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_CLOCK_REFERENCE_H_
