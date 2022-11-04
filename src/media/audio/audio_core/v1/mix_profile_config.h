// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_MIX_PROFILE_CONFIG_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_MIX_PROFILE_CONFIG_H_

#include <lib/zx/time.h>

namespace media::audio {

// Parameters which configure the deadline profile used for mixing threads.
struct MixProfileConfig {
  // The default deadline and period is 10 mSec and our capacity is 4.4 mSec. This means that we
  // will receive 4.4 mSec of CPU time every 10mSec, and that 4.4 mSec may be scheduled at any point
  // during that 10 mSec window.
  static constexpr zx::duration kDefaultCapacity = zx::usec(4'400);
  static constexpr zx::duration kDefaultDeadline = zx::usec(10'000);
  static constexpr zx::duration kDefaultPeriod = zx::usec(10'000);

  // Mix profile capacity to process.
  zx::duration capacity = kDefaultCapacity;

  // Mix profile deadline.
  zx::duration deadline = kDefaultDeadline;

  // Mix profile period.
  zx::duration period = kDefaultPeriod;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_MIX_PROFILE_CONFIG_H_
