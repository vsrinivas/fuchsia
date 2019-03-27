// // Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer_tmp/demux/byte_rate_estimator.h"

#include <math.h>

#include "gtest/gtest.h"

namespace media_player {
namespace {

ByteRateEstimator::ByteRateSample SampleOfRate(size_t byte_rate) {
  return ByteRateEstimator::ByteRateSample{.start_time = zx::time(0),
                                           .stop_time = zx::time(ZX_SEC(1)),
                                           .bytes_processed = byte_rate};
}

TEST(ByteRateEstimator, Estimate) {
  {
    // Estimate should be nullopt if no samples are available.
    ByteRateEstimator under_test(1);
    EXPECT_EQ(under_test.Estimate(), std::nullopt);
  }
  {
    // Should calculate weighted moving average of samples.
    ByteRateEstimator under_test(3);
    under_test.AddSample(SampleOfRate(13));
    under_test.AddSample(SampleOfRate(15));
    under_test.AddSample(SampleOfRate(10));

    std::optional<float> estimate = under_test.Estimate();
    ASSERT_TRUE(estimate.has_value());

    // A flat average would be 12.666... and round to 13. A weighted moving
    // average should be (10*3 + 15*2 + 13)/((3*(3+1))/2) = 12.166...
    EXPECT_EQ(round(*estimate), 12);
  }
  {
    ByteRateEstimator under_test(2);
    under_test.AddSample(SampleOfRate(1000));
    under_test.AddSample(SampleOfRate(10));
    under_test.AddSample(SampleOfRate(10));

    std::optional<float> estimate = under_test.Estimate();
    ASSERT_TRUE(estimate.has_value());

    // If it dropped the first sample as it should have, we'll get an average
    // of 10.
    EXPECT_EQ(round(*estimate), 10);
  }
}

}  // namespace
}  // namespace media_player
