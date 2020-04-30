// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scheduling/frame_stats.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/fit/bridge.h>
#include <lib/fit/defer.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/gtest/test_loop_fixture.h>

#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "lib/inspect/cpp/inspect.h"
#include "lib/inspect/cpp/reader.h"
#include "lib/inspect/service/cpp/reader.h"
#include "lib/inspect/service/cpp/service.h"
#include "lib/inspect/testing/cpp/inspect.h"
#include "src/lib/cobalt/cpp/testing/mock_cobalt_logger.h"
#include "src/ui/scenic/lib/gfx/tests/mocks/mocks.h"
#include "src/ui/scenic/lib/scheduling/frame_timings.h"

namespace scheduling {
namespace test {

using inspect::Hierarchy;
using inspect::Node;
using testing::AllOf;
using testing::IsEmpty;
using testing::SizeIs;
using testing::UnorderedElementsAre;
using namespace inspect::testing;
using cobalt::LogMethod::kLogIntHistogram;

constexpr char kFrameStatsNodeName[] = "FrameStatsTest";

// Note: this is following |garnet/public/lib/inspect/tests/reader_unittest.cc|
// to trigger execution of a LazyStringProperty.
class FrameStatsTest : public gtest::RealLoopFixture {
 public:
  static constexpr char kObjectsName[] = "diagnostics";

  FrameStatsTest() : inspector_(), executor_(dispatcher()) {
    handler_ = inspect::MakeTreeHandler(&inspector_, dispatcher());
  }

  void SchedulePromise(fit::pending_task promise) { executor_.schedule_task(std::move(promise)); }

  // Helper function for test boiler plate.
  fit::result<Hierarchy> ReadInspectVmo() {
    fuchsia::inspect::TreePtr ptr;
    handler_(ptr.NewRequest());
    fit::result<Hierarchy> ret;
    SchedulePromise(inspect::ReadFromTree(std::move(ptr)).then([&](fit::result<Hierarchy>& val) {
      ret = std::move(val);
    }));
    RunLoopUntil([&] { return ret.is_ok() || ret.is_error(); });

    return ret;
  }

 protected:
  inspect::Inspector inspector_;

 private:
  async::Executor executor_;
  fidl::InterfaceRequestHandler<fuchsia::inspect::Tree> handler_;
};

namespace {

// Struct which contains pointers to nodes/properties read from the inspect::Hierarchy
// reported by FrameStats.
struct FrameStatsHierarchyPointers {
  bool AllPointersPopulated() {
    return entire_history && recent_frames && recent_delayed_frames &&
           entire_history_props.total_frame_count && entire_history_props.delayed_frame_count &&
           entire_history_props.dropped_frame_count;
  }

  const inspect::Hierarchy* entire_history = nullptr;
  const inspect::Hierarchy* recent_frames = nullptr;
  const inspect::Hierarchy* recent_delayed_frames = nullptr;

  struct {
    const inspect::UintPropertyValue* total_frame_count = nullptr;
    const inspect::UintPropertyValue* delayed_frame_count = nullptr;
    const inspect::UintPropertyValue* dropped_frame_count = nullptr;
  } entire_history_props;
};

// Returns a newly-populated FrameStatsHierarchyPointers struct.
FrameStatsHierarchyPointers GetFrameStatsHierarchyPointers(inspect::Hierarchy* root) {
  const std::string kEntireHistoryName = "0 - Entire History";
  const std::string kRecentFramesName = "1 - Recent Frame Stats (times in ms)";
  const std::string kRecentDelayedFramesName = "2 - Recent Delayed Frame Stats (times in ms)";
  const std::string kTotalFrameCount = "Total Frame Count";
  const std::string kDelayedFrameCount = "Delayed Frame Count (missed VSYNC)";
  const std::string kDroppedFrameCount = "Dropped Frame Count";

  FrameStatsHierarchyPointers ret;
  ret.entire_history = root->GetByPath({kFrameStatsNodeName, kEntireHistoryName});
  ret.recent_frames = root->GetByPath({kFrameStatsNodeName, kRecentFramesName});
  ret.recent_delayed_frames = root->GetByPath({kFrameStatsNodeName, kRecentDelayedFramesName});

  if (ret.entire_history) {
    ret.entire_history_props.total_frame_count =
        ret.entire_history->node().get_property<inspect::UintPropertyValue>(kTotalFrameCount);

    ret.entire_history_props.delayed_frame_count =
        ret.entire_history->node().get_property<inspect::UintPropertyValue>(kDelayedFrameCount);

    ret.entire_history_props.dropped_frame_count =
        ret.entire_history->node().get_property<inspect::UintPropertyValue>(kDroppedFrameCount);
  }

  return ret;
}

}  // anonymous namespace

TEST_F(FrameStatsTest, SmokeTest_TriggerLazyStringProperties) {
  FrameStats stats(inspector_.GetRoot().CreateChild(kFrameStatsNodeName), nullptr);

  auto root = ReadInspectVmo().take_value();

  auto pointers = GetFrameStatsHierarchyPointers(&root);
  ASSERT_TRUE(pointers.AllPointersPopulated());
  EXPECT_EQ(pointers.entire_history->node().properties().size(), 5U);
  EXPECT_EQ(pointers.recent_frames->node().properties().size(), 4U);
  EXPECT_EQ(pointers.recent_delayed_frames->node().properties().size(), 4U);
}

TEST_F(FrameStatsTest, SmokeTest_DummyFrameTimings) {
  FrameStats stats(inspector_.GetRoot().CreateChild(kFrameStatsNodeName), nullptr);

  const zx::duration vsync_interval = zx::msec(16);
  FrameTimings::Timestamps frame_times = {
      .latch_point_time = zx::time(0) + zx::msec(4),
      .update_done_time = zx::time(0) + zx::msec(6),
      .render_start_time = zx::time(0) + zx::msec(6),
      .render_done_time = zx::time(0) + zx::msec(12),
      .target_presentation_time = zx::time(0) + zx::msec(16),
      .actual_presentation_time = zx::time(0) + zx::msec(16),
  };
  for (int i = 0; i < 200; i++) {
    stats.RecordFrame(frame_times, vsync_interval);

    frame_times.latch_point_time += zx::msec(16);
    frame_times.update_done_time += zx::msec(16);
    frame_times.render_start_time += zx::msec(16);
    frame_times.render_done_time += zx::msec(16);
    frame_times.target_presentation_time += zx::msec(16);
    frame_times.actual_presentation_time += zx::msec(16);
  }

  FrameTimings::Timestamps dropped_times = {.latch_point_time = zx::time(0) + zx::msec(4),
                                            .update_done_time = zx::time(0) + zx::msec(6),
                                            .render_start_time = zx::time(0) + zx::msec(6),
                                            .render_done_time = zx::time(0) + zx::msec(12),
                                            .target_presentation_time = zx::time(0) + zx::msec(16),
                                            .actual_presentation_time = FrameTimings::kTimeDropped};
  for (int i = 0; i < 30; i++) {
    stats.RecordFrame(dropped_times, vsync_interval);

    dropped_times.latch_point_time += zx::msec(16);
    dropped_times.update_done_time += zx::msec(16);
    dropped_times.render_start_time += zx::msec(16);
    dropped_times.render_done_time += zx::msec(16);
    dropped_times.target_presentation_time += zx::msec(16);
  }

  FrameTimings::Timestamps delayed_times = {.latch_point_time = zx::time(0) + zx::msec(4),
                                            .update_done_time = zx::time(0) + zx::msec(6),
                                            .render_start_time = zx::time(0) + zx::msec(6),
                                            .render_done_time = zx::time(0) + zx::msec(22),
                                            .target_presentation_time = zx::time(0) + zx::msec(16),
                                            .actual_presentation_time = zx::time(0) + zx::msec(32)};
  for (int i = 0; i < 20; i++) {
    stats.RecordFrame(delayed_times, vsync_interval);

    delayed_times.latch_point_time = delayed_times.actual_presentation_time + zx::msec(1);
    delayed_times.update_done_time = delayed_times.actual_presentation_time + zx::msec(4);
    delayed_times.render_start_time = delayed_times.actual_presentation_time + zx::msec(4);
    delayed_times.render_done_time = delayed_times.actual_presentation_time + zx::msec(20);
    delayed_times.target_presentation_time = delayed_times.actual_presentation_time + zx::msec(16);
    delayed_times.actual_presentation_time += zx::msec(32);
  }

  auto root = ReadInspectVmo().take_value();
  auto pointers = GetFrameStatsHierarchyPointers(&root);
  ASSERT_TRUE(pointers.AllPointersPopulated());
  EXPECT_EQ(250U, pointers.entire_history_props.total_frame_count->value());
  EXPECT_EQ(30U, pointers.entire_history_props.dropped_frame_count->value());
  EXPECT_EQ(20U, pointers.entire_history_props.delayed_frame_count->value());
}

class FrameStatsCobaltTest : public gtest::TestLoopFixture {};

TEST_F(FrameStatsCobaltTest, LogFrameTimes) {
  cobalt::CallCountMap cobalt_call_counts;
  FrameStats stats(Node(), std::make_unique<cobalt::MockCobaltLogger>(&cobalt_call_counts));

  const zx::duration vsync_interval = zx::msec(16);
  FrameTimings::Timestamps ontime_frame_times = {
      .latch_point_time = zx::time(0) + zx::msec(4),
      .update_done_time = zx::time(0) + zx::msec(6),
      .render_start_time = zx::time(0) + zx::msec(6),
      .render_done_time = zx::time(0) + zx::msec(12),
      .target_presentation_time = zx::time(0) + zx::msec(16),
      .actual_presentation_time = zx::time(0) + zx::msec(16),
  };
  FrameTimings::Timestamps dropped_frame_times = {
      .latch_point_time = zx::time(10) + zx::msec(4),
      .update_done_time = zx::time(10) + zx::msec(6),
      .render_start_time = zx::time(10) + zx::msec(6),
      .render_done_time = zx::time(10) + zx::msec(12),
      .target_presentation_time = zx::time(10) + zx::msec(16),
      .actual_presentation_time = FrameTimings::kTimeDropped};
  FrameTimings::Timestamps delayed_frame_times = {
      .latch_point_time = zx::time(20) + zx::msec(4),
      .update_done_time = zx::time(20) + zx::msec(6),
      .render_start_time = zx::time(20) + zx::msec(6),
      .render_done_time = zx::time(20) + zx::msec(22),
      .target_presentation_time = zx::time(20) + zx::msec(16),
      .actual_presentation_time = zx::time(20) + zx::msec(32)};

  // No frame recorded. No logging needed.
  EXPECT_TRUE(RunLoopFor(FrameStats::kCobaltDataCollectionInterval));
  EXPECT_TRUE(cobalt_call_counts.empty());

  stats.RecordFrame(ontime_frame_times, vsync_interval);
  // Histograms will be flushed into Cobalt. One for on time latch to actual presentation time,
  // one for rendering times
  EXPECT_TRUE(RunLoopFor(FrameStats::kCobaltDataCollectionInterval));
  EXPECT_EQ(cobalt_call_counts[kLogIntHistogram], (uint32_t)2);

  // Since histograms were emptied, there should be no additional cobalt call count.
  EXPECT_TRUE(RunLoopFor(FrameStats::kCobaltDataCollectionInterval));
  EXPECT_EQ(cobalt_call_counts[kLogIntHistogram], (uint32_t)2);

  stats.RecordFrame(ontime_frame_times, vsync_interval);
  stats.RecordFrame(ontime_frame_times, vsync_interval);
  stats.RecordFrame(dropped_frame_times, vsync_interval);
  stats.RecordFrame(delayed_frame_times, vsync_interval);
  stats.RecordFrame(ontime_frame_times, vsync_interval);
  // Expect 4 histograms to be flushed into cobalt. One for rendering times,
  // three for latch to actual presentation times (for ontime, dropped and delayed).
  EXPECT_TRUE(RunLoopFor(FrameStats::kCobaltDataCollectionInterval));
  EXPECT_EQ(cobalt_call_counts[kLogIntHistogram], (uint32_t)(2 + 4));
}

}  // namespace test
}  // namespace scheduling
