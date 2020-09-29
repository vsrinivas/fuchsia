// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/time.h>
#include <unistd.h>

#include <iterator>
#include <limits>
#include <set>
#include <vector>

#include <fs/metrics/histograms.h>
#include <zxtest/zxtest.h>

namespace fs_metrics {
namespace {

class HistogramsTest : public zxtest::Test {
 public:
  HistogramsTest() : inspector_() {}

 protected:
  inspect::Inspector inspector_;
};

constexpr zx::duration kDuration = zx::nsec(5);

const std::vector<EventOptions>& GetOptionsSets() {
  constexpr int64_t kBlockCounts[] = {std::numeric_limits<int64_t>::min(), 1, 5, 31, 32,
                                      std::numeric_limits<int64_t>::max()};
  constexpr int64_t kNodeDepths[] = {
      std::numeric_limits<int64_t>::min(), 1, 2, 4, 8, 16, 32, 64, 128,
      std::numeric_limits<int64_t>::max()};
  constexpr int64_t kNodeDegrees[] = {
      std::numeric_limits<int64_t>::min(), 1, 2, 4, 8, 16, 32, 64, 128, 1024, 1024 * 1024,
      std::numeric_limits<int64_t>::max()};
  constexpr bool kBuffered[] = {true, false};
  constexpr bool kSuccess[] = {true, false};

  static std::vector<EventOptions> option_set;
  option_set.reserve(std::size(kBlockCounts) * std::size(kNodeDepths) * std::size(kNodeDegrees) *
                     std::size(kBuffered) * std::size(kSuccess));

  for (auto block_count : kBlockCounts) {
    for (auto node_depth : kNodeDepths) {
      for (auto node_degree : kNodeDegrees) {
        for (auto buffered : kBuffered) {
          for (auto success : kSuccess) {
            EventOptions options;
            options.block_count = block_count;
            options.node_degree = node_degree;
            options.node_depth = node_depth;
            options.success = success;
            options.buffered = buffered;
            option_set.push_back(options);
          }
        }
      }
    }
  }

  return option_set;
}

TEST_F(HistogramsTest, AllOptionsAreValid) {
  Histograms histograms = Histograms(&inspector_.GetRoot());
  std::set<uint64_t> histogram_ids;

  std::vector<fs_metrics::Event> all_events;
  all_events.insert(all_events.end(), kVnodeEvents, kVnodeEvents + kVnodeEventCount);
  all_events.insert(all_events.end(), kJournalEvents, kJournalEvents + kJournalEventCount);
  for (auto operation : all_events) {
    uint64_t prev_size = histogram_ids.size();
    for (auto option_set : GetOptionsSets()) {
      uint64_t histogram_id = histograms.GetHistogramId(operation, option_set);
      ASSERT_GE(histogram_id, 0);
      ASSERT_LT(histogram_id, histograms.GetHistogramCount());
      histogram_ids.insert(histogram_id);
      histograms.Record(histogram_id, kDuration);
    }
    ASSERT_EQ(histograms.GetHistogramCount(operation), histogram_ids.size() - prev_size,
              " Operation Histogram Count is wrong. %lu", static_cast<uint64_t>(operation));
  }

  ASSERT_EQ(histogram_ids.size(), histograms.GetHistogramCount(),
            "Failed to cover all histograms with all options set.");
}

TEST_F(HistogramsTest, DefaultLatencyEventSmokeTest) {
  Histograms histograms = Histograms(&inspector_.GetRoot());
  std::set<uint64_t> histogram_ids;

  // This is will log an event with default options for every operation, which would crash with
  // unitialized memory.
  for (auto operation : kVnodeEvents) {
    histograms.NewLatencyEvent(operation);
  }
}

TEST_F(HistogramsTest, InvalidOptionsReturnsHistogramCount) {
  Histograms histograms = Histograms(&inspector_.GetRoot());
  ASSERT_EQ(histograms.GetHistogramId(Event::kDataCorruption, EventOptions()),
            histograms.GetHistogramCount());
  ASSERT_EQ(histograms.GetHistogramId(Event::kCompression, EventOptions()),
            histograms.GetHistogramCount());

  uint32_t invalid_event = static_cast<uint32_t>(Event::kInvalidEvent) + 1;
  ASSERT_EQ(histograms.GetHistogramId(static_cast<Event>(invalid_event), EventOptions()),
            histograms.GetHistogramCount());
}

TEST_F(HistogramsTest, SizeIsMultipleOfPageSize) {
  Histograms histograms = Histograms(&inspector_.GetRoot());
  ASSERT_EQ(Histograms::Size() % PAGE_SIZE, 0);
}

}  // namespace
}  // namespace fs_metrics
