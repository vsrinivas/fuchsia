// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/timeline_function_math.h"

#include <lib/syslog/cpp/macros.h>

namespace media_audio {

Fixed TimelineFunctionOffsetInFracFrames(const TimelineFunction& a, const TimelineFunction& b) {
  FX_CHECK(a.rate() == b.rate());

  // Functions are:
  //
  // ```
  // f(x) = (x-x0) * slope + y0
  // ```
  //
  // We compute:
  //
  // ```
  // b(x) = a(x) + offset
  // ```
  //
  // Solving for `offset`, we have:
  //
  // ```
  // offset = b(x) - a(x)
  //        = (x-x0b) * slope + y0b - (x-x0a) * slope - y0a
  //        = (x0a - x0b) * slope + y0b - y0a
  // ```
  const int64_t x0a = a.reference_time();
  const int64_t x0b = b.reference_time();
  const auto y0a = Fixed::FromRaw(a.subject_time());
  const auto y0b = Fixed::FromRaw(b.subject_time());

  return Fixed::FromRaw(a.rate().Scale(x0a - x0b)) + y0b - y0a;
}

}  // namespace media_audio
