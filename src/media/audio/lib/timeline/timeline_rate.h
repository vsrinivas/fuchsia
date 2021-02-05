// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_TIMELINE_TIMELINE_RATE_H_
#define SRC_MEDIA_AUDIO_LIB_TIMELINE_TIMELINE_RATE_H_

#include <stdint.h>
#include <zircon/assert.h>

#include <limits>

namespace media {

// TimelineRate expresses the relative rate of a timeline as the ratio between two uint64_t values
// subject_delta / reference_delta. "subject" refers to the timeline whose rate is being
// represented, and "reference" refers to the timeline relative to which the rate is expressed.
class TimelineRate final {
 public:
  // Used to indicate overflow and underflow of scaling operations.
  static constexpr int64_t kOverflow = std::numeric_limits<int64_t>::max();
  static constexpr int64_t kUnderflow = std::numeric_limits<int64_t>::min();

  // Zero as a TimelineRate.
  static const TimelineRate Zero;

  // Nanoseconds (subject) per second (reference) as a TimelineRate.
  static const TimelineRate NsPerSecond;

  // Returns the product of the rates. If exact is true, crash on precision loss.
  static TimelineRate Product(TimelineRate a, TimelineRate b, bool exact = true);

  TimelineRate() : subject_delta_(0), reference_delta_(1) {}

  // Creates a TimelineRate from a numerator and denominator.
  TimelineRate(uint64_t subject_delta, uint64_t reference_delta);

  // Creates a TimelineRate based on a floating-point value. ASSERTs on negative values.
  explicit TimelineRate(float rate_as_float) : TimelineRate(static_cast<double>(rate_as_float)) {}
  explicit TimelineRate(double rate_as_double)
      : TimelineRate(rate_as_double > 1.0 ? kDoubleFactor
                                          : static_cast<uint64_t>(kDoubleFactor * rate_as_double),
                     rate_as_double > 1.0 ? static_cast<uint64_t>(kDoubleFactor / rate_as_double)
                                          : kDoubleFactor) {
    ZX_DEBUG_ASSERT(rate_as_double >= 0.0);
  }

  // Determines whether this |TimelineRate| is invertible.
  bool invertible() const { return subject_delta_ != 0; }

  // Returns the inverse of the rate. DCHECKs if the subject_delta of this rate is zero.
  TimelineRate Inverse() const {
    ZX_DEBUG_ASSERT(subject_delta_ != 0);

    // Note: TimelineRates should always be in reduced form. Because of this, we need not invoke the
    // subject/reference constructor (which attempts to reduce the ratio). Instead, use the default
    // constructor and just swap subject/reference.
    TimelineRate ret;
    ret.subject_delta_ = reference_delta_;
    ret.reference_delta_ = subject_delta_;
    return ret;
  }

  // Rounding mode for Scale operations.
  enum class RoundingMode {
    Truncate,  // round towards zero
    Floor,     // round down
    Ceiling,   // round up
  };

  // Scales the value by this rate. Returns kOverflow on overflow and kUnderflow on underflow.
  int64_t Scale(int64_t value, RoundingMode mode = RoundingMode::Truncate) const;

  uint64_t subject_delta() const { return subject_delta_; }
  uint64_t reference_delta() const { return reference_delta_; }

 private:
  // Multiplier for double-to-TimelineRate conversion (doubles have fixed bit-width mantissas).
  static constexpr uint64_t kDoubleFactor = 1ul << 52;

  uint64_t subject_delta_;
  uint64_t reference_delta_;
};

// Tests two rates for equality.
inline bool operator==(TimelineRate a, TimelineRate b) {
  return a.subject_delta() == b.subject_delta() && a.reference_delta() == b.reference_delta();
}

// Tests two rates for inequality.
inline bool operator!=(TimelineRate a, TimelineRate b) { return !(a == b); }

}  // namespace media

#endif  // SRC_MEDIA_AUDIO_LIB_TIMELINE_TIMELINE_RATE_H_
