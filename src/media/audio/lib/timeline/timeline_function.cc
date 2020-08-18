// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "timeline_function.h"

#include <zircon/assert.h>

#include <limits>
#include <utility>

namespace media {

// static
// Translates a given reference value through a provided timeline function, producing a
// corresponding subject value. Returns kOverflow if result can't fit in an int64_t.
int64_t TimelineFunction::Apply(int64_t subject_time, int64_t reference_time, TimelineRate rate,
                                int64_t reference_input) {
  int64_t scaled_value = rate.Scale(reference_input - reference_time);
  if (scaled_value == TimelineRate::kOverflow) {
    return TimelineRate::kOverflow;
  }

  int64_t result_value = scaled_value + subject_time;
  auto result_value_128 = static_cast<__int128_t>(scaled_value) + subject_time;
  if (result_value_128 != result_value) {
    return TimelineRate::kOverflow;
  }

  return result_value;
}

// static
// Combine two given timeline functions, forming a new one. ASSERT upon overflow.
TimelineFunction TimelineFunction::Compose(const TimelineFunction& bc, const TimelineFunction& ab,
                                           bool exact) {
  // This composition approach may compromise range and accuracy (in some cases) for simplicity.
  // TODO(mpuryear): re-implement for improved range/accuracy (without much more cost)?
  auto scaled_subject_time = bc.Apply(ab.subject_time());
  ZX_ASSERT(scaled_subject_time != TimelineRate::kOverflow);

  return TimelineFunction(scaled_subject_time, ab.reference_time(),
                          TimelineRate::Product(ab.rate(), bc.rate(), exact));
}

}  // namespace media
