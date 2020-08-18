// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_TIMELINE_TIMELINE_FUNCTION_H_
#define SRC_MEDIA_AUDIO_LIB_TIMELINE_TIMELINE_FUNCTION_H_

#include <zircon/assert.h>

#include "src/media/audio/lib/timeline/timeline_rate.h"

namespace media {

// A linear function from int64_t to int64_t with non-negative slope that translates reference
// timeline values into subject timeline values (the 'subject' being the timeline that's represented
// by the function). The representation is in point-slope form. The point is represented as two
// int64_t time values (subject_time, reference_time), and the slope (rate) is represented as a
// TimelineRate, the ratio of two uint64_t values (subject_delta / reference_delta).
class TimelineFunction final {
 public:
  // Applies a timeline function.
  static int64_t Apply(int64_t subject_time, int64_t reference_time,
                       TimelineRate rate,  // subject_delta / reference_delta
                       int64_t reference_input);

  // Applies the inverse of a timeline function.
  static int64_t ApplyInverse(int64_t subject_time, int64_t reference_time,
                              TimelineRate rate,  // subject_delta / reference_delta
                              int64_t subject_input) {
    ZX_DEBUG_ASSERT(rate.reference_delta() != 0u);
    return Apply(reference_time, subject_time, rate.Inverse(), subject_input);
  }

  // Composes timeline functions B->C and A->B, producing A->C. If exact, ASSERTs on precision loss.
  // TODO(mpuryear): Consider always allowing inexact results.
  static TimelineFunction Compose(const TimelineFunction& bc, const TimelineFunction& ab,
                                  bool exact = true);

  TimelineFunction() : subject_time_(0), reference_time_(0) {}

  TimelineFunction(int64_t subject_time, int64_t reference_time, uint64_t subject_delta,
                   uint64_t reference_delta)
      : subject_time_(subject_time),
        reference_time_(reference_time),
        rate_(subject_delta, reference_delta) {}

  TimelineFunction(int64_t subject_time, int64_t reference_time, TimelineRate rate)
      : subject_time_(subject_time), reference_time_(reference_time), rate_(rate) {}

  explicit TimelineFunction(TimelineRate rate)
      : subject_time_(0), reference_time_(0), rate_(rate) {}

  // Determines whether this |TimelineFunction| is invertible.
  bool invertible() const { return rate_.invertible(); }

  // Applies the function. Returns TimelineRate::kOverflow on overflow.
  int64_t Apply(int64_t reference_input) const {
    return Apply(subject_time_, reference_time_, rate_, reference_input);
  }

  // Applies the inverse of the function. Returns TimelineRate::kOverflow on overflow.
  int64_t ApplyInverse(int64_t subject_input) const {
    ZX_DEBUG_ASSERT(rate_.reference_delta() != 0u);
    return ApplyInverse(subject_time_, reference_time_, rate_, subject_input);
  }

  // Applies the function.  Returns TimelineRate::kOverflow on overflow.
  int64_t operator()(int64_t reference_input) const { return Apply(reference_input); }

  // Returns a timeline function that is the inverse of this timeline function.
  TimelineFunction Inverse() const {
    ZX_DEBUG_ASSERT(rate_.reference_delta() != 0u);
    return TimelineFunction(reference_time_, subject_time_, rate_.Inverse());
  }

  int64_t subject_time() const { return subject_time_; }
  int64_t reference_time() const { return reference_time_; }

  const TimelineRate& rate() const { return rate_; }

  uint64_t subject_delta() const { return rate_.subject_delta(); }
  uint64_t reference_delta() const { return rate_.reference_delta(); }

 private:
  int64_t subject_time_;
  int64_t reference_time_;
  TimelineRate rate_;
};

// Tests two timeline functions for equality. Equality requires equal basis values.
inline bool operator==(const TimelineFunction& a, const TimelineFunction& b) {
  return a.subject_time() == b.subject_time() && a.reference_time() == b.reference_time() &&
         a.rate() == b.rate();
}

// Tests two timeline functions for inequality. Equality requires equal basis values.
inline bool operator!=(const TimelineFunction& a, const TimelineFunction& b) { return !(a == b); }

// Composes two timeline functions B->C and A->B producing A->C. ASSERTs on precision loss.
inline TimelineFunction operator*(const TimelineFunction& bc, const TimelineFunction& ab) {
  return TimelineFunction::Compose(bc, ab);
}

}  // namespace media

#endif  // SRC_MEDIA_AUDIO_LIB_TIMELINE_TIMELINE_FUNCTION_H_
