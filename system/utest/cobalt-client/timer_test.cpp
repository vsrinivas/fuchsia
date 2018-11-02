// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cobalt-client/cpp/histogram-internal.h>
#include <cobalt-client/cpp/histogram.h>
#include <cobalt-client/cpp/timer.h>
#include <lib/zx/time.h>

#include <unittest/unittest.h>

namespace cobalt_client {
namespace internal {
namespace {
// Number of buckets used for histogram.
constexpr uint32_t kBuckets = 2;

// Default id for the histogram.
constexpr uint64_t kMetricId = 1;

// Component name.
constexpr char kComponent[] = "SomeRandomHistogramComponent";

constexpr uint32_t kEventCode = 2;

RemoteHistogram::EventBuffer MakeEventBuffer() {
    return RemoteHistogram::EventBuffer();
}

RemoteMetricInfo MakeRemoteMetricInfo() {
    RemoteMetricInfo metric_info;
    metric_info.metric_id = kMetricId;
    metric_info.component = kComponent;
    metric_info.event_code = kEventCode;
    return metric_info;
}

RemoteHistogram MakeRemoteHistogram() {
    return RemoteHistogram(kBuckets, MakeRemoteMetricInfo(), MakeEventBuffer());
}

template <int64_t return_val>
int64_t TicksToUnitStub(zx::ticks delta) {
    return return_val;
}

bool TestCollectOnDestruction() {
    BEGIN_TEST;
    HistogramOptions options = HistogramOptions::Exponential(/*bucket_count=*/kBuckets, /*base=*/2,
                                                             /*scalar=*/1, /*offset=*/0);
    RemoteHistogram remote_histogram(kBuckets + 2, MakeRemoteMetricInfo(), MakeEventBuffer());
    ASSERT_TRUE(options.IsValid());
    Histogram histogram(&options, &remote_histogram);
    { auto timer = Timer(histogram, /*is_collecting=*/true, TicksToUnitStub<1>); }

    // bucket[1] contains values from 1 to 3 in our time units. Since we are fixating the conversion
    // to return 1 always it will fall into the [1,3) domain, and should increase the count of this
    // bucket by 1.
    EXPECT_EQ(histogram.GetRemoteCount(-1), 0);
    EXPECT_EQ(histogram.GetRemoteCount(0), 0);
    EXPECT_EQ(histogram.GetRemoteCount(1), 1);
    EXPECT_EQ(histogram.GetRemoteCount(3.1), 0);
    END_TEST;
}

bool TestCancel() {
    BEGIN_TEST;
    HistogramOptions options = HistogramOptions::Exponential(/*bucket_count=*/kBuckets, /*base=*/2,
                                                             /*scalar=*/1, /*offset=*/0);
    RemoteHistogram remote_histogram(kBuckets + 2, MakeRemoteMetricInfo(), MakeEventBuffer());
    ASSERT_TRUE(options.IsValid());
    Histogram histogram(&options, &remote_histogram);
    {
        auto timer = Timer(histogram, /*is_collecting=*/true, TicksToUnitStub<1>);
        timer.Cancel();
    }

    // Check every bucket, there should be no logged events.
    EXPECT_EQ(histogram.GetRemoteCount(-1), 0);
    EXPECT_EQ(histogram.GetRemoteCount(0), 0);
    EXPECT_EQ(histogram.GetRemoteCount(1), 0);
    EXPECT_EQ(histogram.GetRemoteCount(3), 0);
    END_TEST;
}

bool TestNotIsCollecting() {
    BEGIN_TEST;
    HistogramOptions options = HistogramOptions::Exponential(/*bucket_count=*/kBuckets, /*base=*/2,
                                                             /*scalar=*/1, /*offset=*/0);
    RemoteHistogram remote_histogram(kBuckets + 2, MakeRemoteMetricInfo(), MakeEventBuffer());
    ASSERT_TRUE(options.IsValid());
    Histogram histogram(&options, &remote_histogram);
    { auto timer = Timer(histogram, /*is_collecting=*/false, TicksToUnitStub<1>); }

    // Check every bucket, there should be no logged events.
    EXPECT_EQ(histogram.GetRemoteCount(-1), 0);
    EXPECT_EQ(histogram.GetRemoteCount(0), 0);
    EXPECT_EQ(histogram.GetRemoteCount(1), 0);
    EXPECT_EQ(histogram.GetRemoteCount(3), 0);
    END_TEST;
}

bool TestEnd() {
    BEGIN_TEST;
    HistogramOptions options = HistogramOptions::Exponential(/*bucket_count=*/kBuckets, /*base=*/2,
                                                             /*scalar=*/1, /*offset=*/0);
    RemoteHistogram remote_histogram(kBuckets + 2, MakeRemoteMetricInfo(), MakeEventBuffer());
    ASSERT_TRUE(options.IsValid());
    Histogram histogram(&options, &remote_histogram);
    {
        auto timer = Timer(histogram, /*is_collecting=*/true, TicksToUnitStub<1>);
        timer.End();
    }

    // bucket[1] contains values from 1 to 3 in our time units. Since we are fixating the conversion
    // to return 1 always it will fall into the [1,3] domain, and should increase the count of this
    // bucket by 1.
    EXPECT_EQ(histogram.GetRemoteCount(-1), 0);
    EXPECT_EQ(histogram.GetRemoteCount(0), 0);
    EXPECT_EQ(histogram.GetRemoteCount(1), 1);
    EXPECT_EQ(histogram.GetRemoteCount(3), 0);
    END_TEST;
}

bool TestMoveConstruct() {
    BEGIN_TEST;
    HistogramOptions options = HistogramOptions::Exponential(/*bucket_count=*/kBuckets, /*base=*/2,
                                                             /*scalar=*/1, /*offset=*/0);
    RemoteHistogram remote_histogram(kBuckets + 2, MakeRemoteMetricInfo(), MakeEventBuffer());
    ASSERT_TRUE(options.IsValid());
    Histogram histogram(&options, &remote_histogram);
    {
        auto timer = Timer(histogram, /*is_collecting=*/true, TicksToUnitStub<1>);
        auto timer_2 = Timer(fbl::move(timer));
    }

    // bucket[1] contains values from 1 to 3 in our time units. Since we are fixating the conversion
    // to return 1 always it will fall into the [1,3] domain, and should increase the count of this
    // bucket by 1.
    EXPECT_EQ(histogram.GetRemoteCount(-1), 0);
    EXPECT_EQ(histogram.GetRemoteCount(0), 0);
    EXPECT_EQ(histogram.GetRemoteCount(1), 1);
    EXPECT_EQ(histogram.GetRemoteCount(3), 0);
    END_TEST;
}

BEGIN_TEST_CASE(TimerTest)
RUN_TEST(TestCollectOnDestruction)
RUN_TEST(TestCancel)
RUN_TEST(TestEnd)
RUN_TEST(TestMoveConstruct)
RUN_TEST(TestNotIsCollecting)
END_TEST_CASE(TimerTest)

} // namespace
} // namespace internal
} // namespace cobalt_client
