// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_AUDIO_TESTS_SHARED_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_AUDIO_TESTS_SHARED_H_

#include <fuchsia/media/cpp/fidl.h>

namespace media::audio::test {

// For operations expected to generate a response, wait __5 minutes__.
//      We do this to avoid flaky results when testing on high-load
//      (high-latency) environments. For reference, in mid-2018 when observing
//      highly-loaded local QEMU instances running code that correctly generated
//      completion responses, we observed timeouts if waiting 20 ms, but not
//      when waiting 50 ms. This value will be 15000x that (!), and WELL beyond
//      the limit of any human acceptability, so shouldn't exhibit flakiness.
//
// Conversely, when we DO expect a timeout, wait only __50 ms__.
//      Normal response is <5 ms, usually <1 ms on well-performing systems.
//
// These two values codify the following ordered priorities:
//      1) False-positive test failures are expensive and must be eliminated.
//      2) Having done that, streamline test run-time (time=resources=cost);
//      2a) Also, avoid false-negatives (minimize undetected regressions).
//
// Finally, when waiting for a timeout, our granularity (how frequently we check
// for response) can be coarse (the default is every 10 ms). However, when
// expecting a response we can save time by checking more frequently. Restated,
// kDurationResponseExpected should ALWAYS use kDurationGranularity, and
// kDurationTimeoutExpected need NEVER do so.
//
// TODO(mpuryear): Refactor tests to eliminate "wait for nothing bad to happen".
constexpr zx::duration kDurationResponseExpected = zx::sec(300);
constexpr zx::duration kDurationTimeoutExpected = zx::msec(50);
constexpr zx::duration kDurationGranularity = zx::msec(1);

constexpr char kConnectionErr[] =
    "Connection to fuchsia.media FIDL interface was lost!\n";
constexpr char kTimeoutErr[] = "Timeout -- no callback received!\n";
constexpr char kNoTimeoutErr[] = "Unexpected callback received!\n";

constexpr float kUnityGainDb = 0.0f;
constexpr float kTooLowGainDb = fuchsia::media::audio::MUTED_GAIN_DB - 0.1f;
constexpr float kTooHighGainDb = fuchsia::media::audio::MAX_GAIN_DB + 0.1f;

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_AUDIO_TESTS_SHARED_H_
