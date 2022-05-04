// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/helpers/lead_time_watcher.h"

#include <gtest/gtest.h>

namespace fmlib {
namespace {

// Tests simple use of |Watch| and |Report|.
TEST(LeadTimeWatcher, WatchAndReport) {
  constexpr int64_t kMin = 0;
  constexpr int64_t kMax = 0;
  constexpr int64_t kReported = 1;

  LeadTimeWatcher under_test;
  fuchsia::media2::WatchLeadTimeResult callback_lead_time;
  under_test.Watch(kMin, kMax,
                   [&callback_lead_time](fuchsia::media2::WatchLeadTimeResult lead_time) {
                     callback_lead_time = std::move(lead_time);
                   });

  // The callback should not run right away, because no value has been reported.
  EXPECT_EQ(fuchsia::media2::WatchLeadTimeResult::Tag::Invalid, callback_lead_time.Which());

  // After this call, the callback should have run, because |kReported| is out of range.
  under_test.Report(zx::duration(kReported));
  EXPECT_EQ(fuchsia::media2::WatchLeadTimeResult::Tag::kValue, callback_lead_time.Which());
  EXPECT_EQ(kReported, callback_lead_time.value());
}

// Tests simple use of |Watch| and |ReportUnderflow|.
TEST(LeadTimeWatcher, WatchAndReportUnderflow) {
  constexpr int64_t kMin = 0;
  constexpr int64_t kMax = 0;

  LeadTimeWatcher under_test;
  fuchsia::media2::WatchLeadTimeResult callback_lead_time;
  under_test.Watch(kMin, kMax,
                   [&callback_lead_time](fuchsia::media2::WatchLeadTimeResult lead_time) {
                     callback_lead_time = std::move(lead_time);
                   });

  // The callback should not run right away, because no value has been reported.
  EXPECT_EQ(fuchsia::media2::WatchLeadTimeResult::Tag::Invalid, callback_lead_time.Which());

  // After this call, the callback should have run, because underflow is out of range.
  under_test.ReportUnderflow();
  EXPECT_EQ(fuchsia::media2::WatchLeadTimeResult::Tag::kUnderflow, callback_lead_time.Which());
}

// Tests a second |Watch| call terminating an initial |Watch| call.
TEST(LeadTimeWatcher, WatchAndWatch) {
  constexpr int64_t kMin = 0;
  constexpr int64_t kMax = 0;

  LeadTimeWatcher under_test;
  fuchsia::media2::WatchLeadTimeResult first_callback_lead_time;
  under_test.Watch(kMin, kMax,
                   [&first_callback_lead_time](fuchsia::media2::WatchLeadTimeResult lead_time) {
                     first_callback_lead_time = std::move(lead_time);
                   });

  // The first callback should not run right away, because no value has been reported.
  EXPECT_EQ(fuchsia::media2::WatchLeadTimeResult::Tag::Invalid, first_callback_lead_time.Which());

  fuchsia::media2::WatchLeadTimeResult second_callback_lead_time;
  under_test.Watch(kMin, kMax,
                   [&second_callback_lead_time](fuchsia::media2::WatchLeadTimeResult lead_time) {
                     second_callback_lead_time = std::move(lead_time);
                   });

  // The first callback should have run, having been terminated by the second.
  EXPECT_EQ(fuchsia::media2::WatchLeadTimeResult::Tag::kNoValue, first_callback_lead_time.Which());

  // The second callback should not run.
  EXPECT_EQ(fuchsia::media2::WatchLeadTimeResult::Tag::Invalid, second_callback_lead_time.Which());
}

// Tests |Report| followed by |Watch|.
TEST(LeadTimeWatcher, ReportAndWatch) {
  constexpr int64_t kMin = 0;
  constexpr int64_t kMax = 0;
  constexpr int64_t kReported = 1;

  LeadTimeWatcher under_test;
  under_test.Report(zx::duration(kReported));

  fuchsia::media2::WatchLeadTimeResult callback_lead_time;
  under_test.Watch(kMin, kMax,
                   [&callback_lead_time](fuchsia::media2::WatchLeadTimeResult lead_time) {
                     callback_lead_time = std::move(lead_time);
                   });

  // The callback should run immediately, because |kReported| was out of range and already
  // reported.
  under_test.Report(zx::duration(kReported));
  EXPECT_EQ(fuchsia::media2::WatchLeadTimeResult::Tag::kValue, callback_lead_time.Which());
  EXPECT_EQ(kReported, callback_lead_time.value());
}

// Tests |ReportUnderflow| followed by |Watch|.
TEST(LeadTimeWatcher, ReportUnderfowAndWatch) {
  constexpr int64_t kMin = 0;
  constexpr int64_t kMax = 0;

  LeadTimeWatcher under_test;
  under_test.ReportUnderflow();

  fuchsia::media2::WatchLeadTimeResult callback_lead_time;
  under_test.Watch(kMin, kMax,
                   [&callback_lead_time](fuchsia::media2::WatchLeadTimeResult lead_time) {
                     callback_lead_time = std::move(lead_time);
                   });

  // The callback should run immediately, because underflow was out of range and already
  // reported.
  under_test.ReportUnderflow();
  EXPECT_EQ(fuchsia::media2::WatchLeadTimeResult::Tag::kUnderflow, callback_lead_time.Which());
}

// Tests that |Watch| remains pending for in-range values and completes for a later out-of-range
// value.
TEST(LeadTimeWatcher, InToOutOfRange) {
  constexpr int64_t kMin = 0;
  constexpr int64_t kMax = 5;
  constexpr int64_t kOutOfRange = kMax + 1;

  LeadTimeWatcher under_test;
  under_test.Report(zx::duration(kMin));

  fuchsia::media2::WatchLeadTimeResult callback_lead_time;
  under_test.Watch(kMin, kMax,
                   [&callback_lead_time](fuchsia::media2::WatchLeadTimeResult lead_time) {
                     callback_lead_time = std::move(lead_time);
                   });

  // The callback should not run right away, because |kMin| is in range.
  EXPECT_EQ(fuchsia::media2::WatchLeadTimeResult::Tag::Invalid, callback_lead_time.Which());

  for (int64_t value = kMin; value <= kMax; ++value) {
    under_test.Report(zx::duration(value));
  }

  // The callback should not run right away, because all values reported were in range.
  EXPECT_EQ(fuchsia::media2::WatchLeadTimeResult::Tag::Invalid, callback_lead_time.Which());

  // After this call, the callback should have run, because |kReported| is out of range.
  under_test.Report(zx::duration(kOutOfRange));
  EXPECT_EQ(fuchsia::media2::WatchLeadTimeResult::Tag::kValue, callback_lead_time.Which());
  EXPECT_EQ(kOutOfRange, callback_lead_time.value());
}

// Tests that |Watch| remains pending for in-range values and completes for a later underflow.
TEST(LeadTimeWatcher, InToUnderflow) {
  constexpr int64_t kMin = 0;
  constexpr int64_t kMax = 5;

  LeadTimeWatcher under_test;
  under_test.Report(zx::duration(kMin));

  fuchsia::media2::WatchLeadTimeResult callback_lead_time;
  under_test.Watch(kMin, kMax,
                   [&callback_lead_time](fuchsia::media2::WatchLeadTimeResult lead_time) {
                     callback_lead_time = std::move(lead_time);
                   });

  // The callback should not run right away, because |kMin| is in range.
  EXPECT_EQ(fuchsia::media2::WatchLeadTimeResult::Tag::Invalid, callback_lead_time.Which());

  for (int64_t value = kMin; value <= kMax; ++value) {
    under_test.Report(zx::duration(value));
  }

  // The callback should not run right away, because all values reported were in range.
  EXPECT_EQ(fuchsia::media2::WatchLeadTimeResult::Tag::Invalid, callback_lead_time.Which());

  // After this call, the callback should have run, because underflow is out of range.
  under_test.ReportUnderflow();
  EXPECT_EQ(fuchsia::media2::WatchLeadTimeResult::Tag::kUnderflow, callback_lead_time.Which());
}

// Tests that underflow is equivalent to -1ns for the purposes of range testing.
TEST(LeadTimeWatcher, UnderflowRangeValue) {
  constexpr int64_t kMin = -1;
  constexpr int64_t kMax = -1;
  constexpr int64_t kReported = 0;

  LeadTimeWatcher under_test;
  fuchsia::media2::WatchLeadTimeResult callback_lead_time;
  under_test.Watch(kMin, kMax,
                   [&callback_lead_time](fuchsia::media2::WatchLeadTimeResult lead_time) {
                     callback_lead_time = std::move(lead_time);
                   });

  // The callback should not run right away, because no value has been reported.
  EXPECT_EQ(fuchsia::media2::WatchLeadTimeResult::Tag::Invalid, callback_lead_time.Which());

  // The callback should not run for underflow, because it's in range.
  under_test.ReportUnderflow();
  EXPECT_EQ(fuchsia::media2::WatchLeadTimeResult::Tag::Invalid, callback_lead_time.Which());

  // After this call, the callback should have run, because |kReported| is out of range.
  under_test.Report(zx::duration(kReported));
  EXPECT_EQ(fuchsia::media2::WatchLeadTimeResult::Tag::kValue, callback_lead_time.Which());
  EXPECT_EQ(kReported, callback_lead_time.value());
}

// Tests the |RespondAndReset| method.
TEST(LeadTimeWatcher, RespondAndReset) {
  constexpr int64_t kMin = 0;
  constexpr int64_t kMax = 0;
  constexpr int64_t kReported = 0;

  LeadTimeWatcher under_test;
  under_test.Report(zx::duration(kReported));

  {
    fuchsia::media2::WatchLeadTimeResult callback_lead_time;
    under_test.Watch(kMin, kMax,
                     [&callback_lead_time](fuchsia::media2::WatchLeadTimeResult lead_time) {
                       callback_lead_time = std::move(lead_time);
                     });

    // The callback should not run right away, because |kReported| is in-range.
    EXPECT_EQ(fuchsia::media2::WatchLeadTimeResult::Tag::Invalid, callback_lead_time.Which());

    // The callback should run after this call, returning the in-range value.
    under_test.RespondAndReset();
    EXPECT_EQ(fuchsia::media2::WatchLeadTimeResult::Tag::kValue, callback_lead_time.Which());
    EXPECT_EQ(kReported, callback_lead_time.value());
  }

  {
    fuchsia::media2::WatchLeadTimeResult callback_lead_time;
    under_test.Watch(kMin, kMax,
                     [&callback_lead_time](fuchsia::media2::WatchLeadTimeResult lead_time) {
                       callback_lead_time = std::move(lead_time);
                     });

    // The callback should not run right away, because we're back in no-value state.
    EXPECT_EQ(fuchsia::media2::WatchLeadTimeResult::Tag::Invalid, callback_lead_time.Which());

    // The callback should run after this call, returning no value.
    under_test.RespondAndReset();
    EXPECT_EQ(fuchsia::media2::WatchLeadTimeResult::Tag::kNoValue, callback_lead_time.Which());
  }
}

}  // namespace
}  // namespace fmlib
