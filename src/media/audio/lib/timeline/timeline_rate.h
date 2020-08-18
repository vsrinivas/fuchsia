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
  // Used to indicate overflow of scaling operations.
  static constexpr int64_t kOverflow = std::numeric_limits<int64_t>::max();

  // Zero as a TimelineRate.
  static const TimelineRate Zero;

  // Nanoseconds (subject) per second (reference) as a TimelineRate.
  static const TimelineRate NsPerSecond;

  // Reduces the ratio of *subject_delta and *reference_delta.
  static void Reduce(uint64_t* subject_delta, uint64_t* reference_delta);

  // Produces the product of the rates. If exact is true, ASSERTs on precision loss.
  // TODO(mpuryear): Consider always allowing inexact results.
  static void Product(uint64_t a_subject_delta, uint64_t a_reference_delta,
                      uint64_t b_subject_delta, uint64_t b_reference_delta,
                      uint64_t* product_subject_delta, uint64_t* product_reference_delta,
                      bool exact = true);

  // Produces the product of the rates and the int64_t as an int64_t. Returns kOverflow on overflow.
  static int64_t Scale(int64_t value, uint64_t subject_delta, uint64_t reference_delta);

  // Returns the product of the rates. If exact is true, ASSERTs on precision loss.
  // TODO(mpuryear): Consider always allowing inexact results.
  static TimelineRate Product(TimelineRate a, TimelineRate b, bool exact = true) {
    uint64_t result_subject_delta;
    uint64_t result_reference_delta;
    Product(a.subject_delta(), a.reference_delta(), b.subject_delta(), b.reference_delta(),
            &result_subject_delta, &result_reference_delta, exact);
    return TimelineRate(result_subject_delta, result_reference_delta);
  }

  TimelineRate() : TimelineRate(0ul) {}

  explicit TimelineRate(uint32_t subject_delta)
      : TimelineRate(static_cast<uint64_t>(subject_delta)) {}
  explicit TimelineRate(uint64_t subject_delta)
      : subject_delta_(subject_delta), reference_delta_(1) {}

  // Creates a TimelineRate based on a floating-point value. ASSERTs on negative values.
  explicit TimelineRate(float rate_as_float) : TimelineRate(static_cast<double>(rate_as_float)) {}
  explicit TimelineRate(double rate_as_double)
      : TimelineRate(rate_as_double > 1.0 ? kDoubleFactor
                                          : static_cast<uint64_t>(kDoubleFactor * rate_as_double),
                     rate_as_double > 1.0 ? static_cast<uint64_t>(kDoubleFactor / rate_as_double)
                                          : kDoubleFactor) {
    ZX_DEBUG_ASSERT(rate_as_double >= 0.0);
  }

  TimelineRate(uint64_t subject_delta, uint64_t reference_delta)
      : subject_delta_(subject_delta), reference_delta_(reference_delta) {
    ZX_DEBUG_ASSERT(reference_delta != 0);
    Reduce(&subject_delta_, &reference_delta_);
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

  // Scales the value by this rate. Returns kOverflow on overflow.
  int64_t Scale(int64_t value) const { return Scale(value, subject_delta_, reference_delta_); }

  // Scales the value by the inverse of this rate.
  int64_t ScaleInverse(int64_t value) const {
    return Scale(value, reference_delta_, subject_delta_);
  }

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

// Returns the ratio of the two rates. ASSERTs on precision loss.
inline TimelineRate operator/(TimelineRate a, TimelineRate b) {
  return TimelineRate::Product(a, b.Inverse());
}

// Returns the product of two rates. ASSERTs on precision loss.
inline TimelineRate operator*(TimelineRate a, TimelineRate b) {
  return TimelineRate::Product(a, b);
}

// Returns the product of a rate and an int64_t. Returns kOverflow on overflow.
inline int64_t operator*(TimelineRate a, int64_t b) { return a.Scale(b); }

// Returns the product of an int64_t and a rate. Returns kOverflow on overflow.
inline int64_t operator*(int64_t a, TimelineRate b) { return b.Scale(a); }

// Returns the quotient of an int64_t by a rate (this equals the product of that int64_t with that
// rate's inverse). Returns kOverflow on overflow.
inline int64_t operator/(int64_t a, TimelineRate b) { return b.ScaleInverse(a); }

}  // namespace media

#endif  // SRC_MEDIA_AUDIO_LIB_TIMELINE_TIMELINE_RATE_H_
