// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_CLOCK_UNREADABLE_CLOCK_H_
#define SRC_MEDIA_AUDIO_LIB_CLOCK_UNREADABLE_CLOCK_H_

#include <memory>

#include "src/media/audio/lib/clock/clock.h"

namespace media_audio {

// This class is essentually just a `std::shared_ptr<const Clock>` but does not export any methods
// except `Clock::koid()`. Reading the clock must be done through some other mechanism, such as a
// `ClockSnapshots` object. Two UnreadableClocks are equivalent iff they reference the same
// underlying Clock object.
class UnreadableClock {
 public:
  explicit UnreadableClock(std::shared_ptr<const Clock> clock) : clock_(std::move(clock)) {}

  // Reports the clock's koid.
  zx_koid_t koid() const { return clock_->koid(); }

  // Reports whether two UnreadableClocks reference the same clock.
  bool operator==(const UnreadableClock& rhs) const { return clock_ == rhs.clock_; }
  bool operator!=(const UnreadableClock& rhs) const { return clock_ != rhs.clock_; }

  // Reports whether an UnreadableClock holds a given `std::shared_ptr<const Clock>`.
  friend bool operator==(const UnreadableClock& lhs, const std::shared_ptr<const Clock>& rhs);
  friend bool operator!=(const UnreadableClock& lhs, const std::shared_ptr<const Clock>& rhs);
  friend bool operator==(const std::shared_ptr<const Clock>& lhs, const UnreadableClock& rhs);
  friend bool operator!=(const std::shared_ptr<const Clock>& lhs, const UnreadableClock& rhs);

 private:
  std::shared_ptr<const Clock> clock_;
};

inline bool operator==(const UnreadableClock& lhs, const std::shared_ptr<const Clock>& rhs) {
  return lhs.clock_ == rhs;
}
inline bool operator!=(const UnreadableClock& lhs, const std::shared_ptr<const Clock>& rhs) {
  return lhs.clock_ != rhs;
}
inline bool operator==(const std::shared_ptr<const Clock>& lhs, const UnreadableClock& rhs) {
  return lhs == rhs.clock_;
}
inline bool operator!=(const std::shared_ptr<const Clock>& lhs, const UnreadableClock& rhs) {
  return lhs == rhs.clock_;
}

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_LIB_CLOCK_UNREADABLE_CLOCK_H_
