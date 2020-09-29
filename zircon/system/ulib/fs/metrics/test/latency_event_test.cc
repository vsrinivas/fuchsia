// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fzl/time.h>
#include <lib/zx/time.h>

#include <vector>

#include <fs/metrics/events.h>
#include <fs/metrics/histograms.h>
#include <zxtest/zxtest.h>

namespace fs_metrics {
namespace {
class FakeClock {
 public:
  static zx::ticks now() { return now_; }

  static void set_now(zx::ticks ticks) { now_ = ticks; }

 private:
  static zx::ticks now_;
};

struct HistogramEntry {
  uint64_t histogram_id;
  zx::duration duration;
};

zx::ticks FakeClock::now_;

class FakeHistograms {
 public:
  uint64_t GetHistogramId(Event operation, const EventOptions& options) const {
    return histogram_id_;
  }

  void Record(uint64_t histogram_id, zx::duration duration) {
    collected_data_.push_back({histogram_id, duration});
  }

  void SetHistogramId(uint64_t id) { histogram_id_ = id; }

  const std::vector<HistogramEntry>& collected_data() const { return collected_data_; }

 private:
  uint64_t histogram_id_ = -1;
  std::vector<HistogramEntry> collected_data_;
};

using FakeLatencyEvent = internal::LatencyEventInternal<FakeHistograms, FakeClock>;

constexpr Event kEvent = Event::kRead;
constexpr zx::ticks kStartTime = zx::ticks(5);
constexpr zx::ticks kEventTicks = zx::ticks(45);

// Cannot be constexpr because fzl::TicksToNs is not constexpr.
const zx::duration kEventDuration = fzl::TicksToNs(kEventTicks);

// Fixture for resetting time between each test.
class LatencyEventTest : public zxtest::Test {
 public:
  void SetUp() override { FakeClock::set_now(kStartTime); }
  void TearDown() override { FakeClock::set_now(kStartTime); }
};

TEST_F(LatencyEventTest, RecordZero) {
  FakeHistograms histograms;
  FakeClock::set_now(zx::ticks(0));
  FakeLatencyEvent event(&histograms, kEvent);

  event.Record();

  ASSERT_TRUE(histograms.collected_data().empty());
}

TEST_F(LatencyEventTest, RecordNonZeroDelta) {
  FakeHistograms histograms;
  EventOptions options;
  FakeLatencyEvent event(&histograms, kEvent);
  *event.mutable_options() = options;

  FakeClock::set_now(kEventTicks + kStartTime);
  event.Record();

  ASSERT_EQ(histograms.collected_data().size(), 1);
  HistogramEntry entry = histograms.collected_data()[0];

  ASSERT_EQ(entry.histogram_id, histograms.GetHistogramId(kEvent, options));
  ASSERT_EQ(entry.duration, kEventDuration);
}

TEST_F(LatencyEventTest, RecordCancelledEventIsIgnored) {
  FakeHistograms histograms;
  FakeLatencyEvent event(&histograms, kEvent);

  event.Cancel();
  FakeClock::set_now(kEventTicks + kStartTime);
  event.Record();

  ASSERT_TRUE(histograms.collected_data().empty());
}

TEST_F(LatencyEventTest, RecordZeroOnDestruction) {
  FakeHistograms histograms;
  FakeClock::set_now(zx::ticks(0));

  { FakeLatencyEvent event(&histograms, kEvent); }

  ASSERT_TRUE(histograms.collected_data().empty());
}

TEST_F(LatencyEventTest, RecordNonZeroDeltaOnDestruction) {
  FakeHistograms histograms;
  EventOptions options;

  {
    FakeLatencyEvent event(&histograms, kEvent);
    *event.mutable_options() = options;

    FakeClock::set_now(kEventTicks + kStartTime);
    event.Record();
  }

  ASSERT_EQ(histograms.collected_data().size(), 1);
  auto entry = histograms.collected_data()[0];

  ASSERT_EQ(entry.histogram_id, histograms.GetHistogramId(kEvent, options));
  ASSERT_EQ(entry.duration.get(), kEventDuration.get());
}

TEST_F(LatencyEventTest, RecordCancelledEventIsIgnoredonDestruction) {
  FakeHistograms histograms;

  {
    FakeLatencyEvent event(&histograms, kEvent);

    FakeClock::set_now(kEventTicks + kStartTime);
    event.Cancel();
    event.Record();
  }

  ASSERT_TRUE(histograms.collected_data().empty());
}

TEST_F(LatencyEventTest, MovedObjectDoesNotLogData) {
  FakeHistograms histograms;
  EventOptions options;

  {
    // This event will not log data because it was moved from.
    FakeLatencyEvent event(&histograms, kEvent);
    {
      FakeLatencyEvent move_to_event = std::move(event);
      *move_to_event.mutable_options() = options;
      FakeClock::set_now(kEventTicks + kStartTime);
    }
  }

  // Only |move_to_event| should log data, since |event| was moved from.
  ASSERT_EQ(histograms.collected_data().size(), 1);
  auto entry = histograms.collected_data()[0];

  ASSERT_EQ(entry.histogram_id, histograms.GetHistogramId(kEvent, options));
  ASSERT_EQ(entry.duration.get(), kEventDuration.get());
}

}  // namespace
}  // namespace fs_metrics
