// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "timeline_function.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/assert.h>

#include <limits>
#include <utility>

namespace media {

// static
// Translates a given reference value through a provided timeline function, producing a
// corresponding subject value. Returns kOverflow if result can't fit in an int64_t.
int64_t TimelineFunction::Apply(int64_t subject_time, int64_t reference_time, TimelineRate rate,
                                int64_t reference_input) {
  // Round down when scaling. This ensures that we preserve the scaled distance between positive
  // and negative points on the timeline. For example, suppose we call this twice:
  //
  //   1. reference_input - reference_time = 20, ratio = 1/8, scaled_value = 2.5
  //   2. reference_input - reference_time = -20, ratio = 1/8, scaled_value = -2.5
  //
  // If we truncate, the scaled values are 2 and -2, which have a difference of 4, while the true
  // scaled difference should be 40*1/8 = 5. However, if we round down, the scaled values are
  // 2 and -3, which have a difference of 5.
  int64_t scaled_value =
      rate.Scale(reference_input - reference_time, TimelineRate::RoundingMode::Floor);
  if (scaled_value == TimelineRate::kOverflow || scaled_value == TimelineRate::kUnderflow) {
    return scaled_value;
  }

  int64_t result_value = scaled_value + subject_time;
  auto result_value_128 = static_cast<__int128_t>(scaled_value) + subject_time;
  if (result_value_128 != result_value) {
    if (result_value_128 > 0) {
      return TimelineRate::kOverflow;
    }
    return TimelineRate::kUnderflow;
  }

  return result_value;
}

// static
// Combine two given timeline functions, forming a new one. ASSERT upon overflow.
TimelineFunction TimelineFunction::Compose(const TimelineFunction& bc, const TimelineFunction& ab,
                                           bool exact) {
  // This composition approach may compromise range and accuracy (in some cases) for simplicity.
  // TODO(fxbug.dev/13293): more accuracy here
  auto scaled_subject_time = bc.Apply(ab.subject_time());
  if (exact) {
    ZX_ASSERT(scaled_subject_time != TimelineRate::kOverflow);
    ZX_ASSERT(scaled_subject_time != TimelineRate::kUnderflow);
  }

  return TimelineFunction(scaled_subject_time, ab.reference_time(),
                          TimelineRate::Product(ab.rate(), bc.rate(), exact));
}

}  // namespace media
