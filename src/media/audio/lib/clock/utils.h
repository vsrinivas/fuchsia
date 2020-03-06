// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_CLOCK_UTILS_H_
#define SRC_MEDIA_AUDIO_LIB_CLOCK_UTILS_H_

#include <lib/affine/transform.h>
#include <lib/fit/result.h>
#include <lib/media/cpp/timeline_function.h>
#include <lib/zx/clock.h>

namespace media::audio {

constexpr uint32_t kInvalidClockGeneration = 0xFFFFFFFF;
struct ClockSnapshot {
  TimelineFunction timeline_transform;
  uint32_t generation = kInvalidClockGeneration;
};

zx_status_t DuplicateClock(const zx::clock& original_clock, zx::clock* dupe_optimal_clock);

zx_status_t GetAndDisplayClockDetails(const zx::clock& ref_clock);
void DisplayClockDetails(const zx_clock_details_v1_t& clock_details);

fit::result<ClockSnapshot, zx_status_t> SnapshotClock(const zx::clock& ref_clock);

fit::result<zx::time, zx_status_t> ReferenceTimeFromMonotonicTime(const zx::clock& ref_clock,
                                                                  zx::time mono_time);
fit::result<zx::time, zx_status_t> MonotonicTimeFromReferenceTime(const zx::clock& ref_clock,
                                                                  zx::time ref_time);

affine::Transform ToAffineTransform(TimelineFunction& tl_function);
TimelineFunction ToTimelineFunction(affine::Transform affine_trans);

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_LIB_CLOCK_UTILS_H_
