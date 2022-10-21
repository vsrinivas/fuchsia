// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_TIMELINE_FUNCTION_MATH_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_TIMELINE_FUNCTION_MATH_H_

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/timeline/timeline_function.h"
#include "src/media/audio/services/mixer/common/basic_types.h"

namespace media_audio {

// Returns the offset `b - a`. Both functions must have the same slope. Each function is:
//
// ```
// f(x) = (x-x0) * slope + y0
// ```
//
// We assume these functions convert from time to fractional frames, meaning `x` is an int64_t time
// while `y` is a Fixed frame number. Hence the offset is a Fixed.
//
// REQUIRED: a.rate() == b.rate()
Fixed TimelineFunctionOffsetInFracFrames(const TimelineFunction& a, const TimelineFunction& b);

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_TIMELINE_FUNCTION_MATH_H_
