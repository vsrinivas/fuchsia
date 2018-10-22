// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

namespace media {
namespace audio {
namespace test {

// For operations expected to complete, wait five seconds to avoid flaky test
// behavior in high-load (high-latency) test environments. For reference, today
// on highly-loaded QEMU instances we see timeouts if we wait 20 ms, but not if
// we wait 50 ms. This value is a full 100x that value, so shouldn't be flaky.
//
// Conversely, when we DO expect a timeout, wait 50 ms (normal response is <5
// ms, usually <1). These values codify the following ordered priorities:
// 1) False-positive test failures are expensive and must be eliminated;
// 2) Having satisfying #1, streamline test-run-time (time=resources=cost);
// 3) Minimize false-negative test outcomes (undetected regressions).
//
constexpr zx::duration kDurationResponseExpected = zx::sec(5);
constexpr zx::duration kDurationTimeoutExpected = zx::msec(50);

constexpr char kConnectionErr[] =
    "Connection to fuchsia.media.Audio svc was lost!\n";

}  // namespace test
}  // namespace audio
}  // namespace media
