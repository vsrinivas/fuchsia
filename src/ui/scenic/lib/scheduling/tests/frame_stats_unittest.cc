// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scheduling/frame_stats.h"

#include <fuchsia/inspect/deprecated/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/fit/bridge.h>
#include <lib/fit/defer.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/gtest/test_loop_fixture.h>

#include <thread>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/cobalt/cpp/testing/mock_cobalt_logger.h"
#include "src/lib/inspect_deprecated/inspect.h"
#include "src/lib/inspect_deprecated/reader.h"
#include "src/lib/inspect_deprecated/testing/inspect.h"
#include "src/ui/scenic/lib/gfx/tests/mocks/mocks.h"
#include "src/ui/scenic/lib/scheduling/frame_timings.h"

namespace scheduling {
namespace test {

using inspect_deprecated::Node;
using inspect_deprecated::ObjectHierarchy;
using testing::AllOf;
using testing::IsEmpty;
using testing::SizeIs;
using testing::UnorderedElementsAre;
using namespace inspect_deprecated::testing;
using cobalt::LogMethod::kLogIntHistogram;

constexpr char kFrameStatsNodeName[] = "FrameStatsTest";

// Note: this is following |garnet/public/lib/inspect/tests/reader_unittest.cc|
// to trigger execution of a LazyStringProperty.
class FrameStatsTest : public gtest::RealLoopFixture {
 public:
  static constexpr char kObjectsName[] = "objects";

  FrameStatsTest()
      : object_(component::Object::Make(kObjectsName)),
        root_object_(component::ObjectDir(object_)),
        executor_(dispatcher()),
        server_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    fuchsia::inspect::deprecated::InspectSyncPtr ptr;
    zx::channel server_channel = ptr.NewRequest().TakeChannel();
    server_thread_ = std::thread([this, server_channel = std::move(server_channel)]() mutable {
      async_set_default_dispatcher(server_loop_.dispatcher());
      fidl::Binding<fuchsia::inspect::deprecated::Inspect> binding(
          object_.get(), std::move(server_channel), server_loop_.dispatcher());

      server_loop_.Run();
    });
    client_ = ptr.Unbind();
  }

  ~FrameStatsTest() override {
    server_loop_.Quit();
    server_thread_.join();
  }

  void SchedulePromise(fit::pending_task promise) { executor_.schedule_task(std::move(promise)); }

  // Helper function for test boiler plate.
  fit::result<fuchsia::inspect::deprecated::Object> ReadInspectVmo() {
    inspect_deprecated::ObjectReader reader(std::move(client_));
    fit::result<fuchsia::inspect::deprecated::Object> result;
    SchedulePromise(reader.OpenChild(kFrameStatsNodeName)
                        .and_then([](inspect_deprecated::ObjectReader& child_reader) {
                          return child_reader.Read();
                        })
                        .then([&](fit::result<fuchsia::inspect::deprecated::Object>& res) {
                          result = std::move(res);
                        }));
    RunLoopUntil([&] { return !!result; });

    return result;
  }

 protected:
  std::shared_ptr<component::Object> object_;
  inspect_deprecated::Node root_object_;
  fidl::InterfaceHandle<fuchsia::inspect::deprecated::Inspect> client_;

 private:
  async::Executor executor_;
  std::thread server_thread_;
  async::Loop server_loop_;
};

TEST_F(FrameStatsTest, SmokeTest_TriggerLazyStringProperties) {
  FrameStats stats(root_object_.CreateChild(kFrameStatsNodeName), nullptr);

  fit::result<fuchsia::inspect::deprecated::Object> result = ReadInspectVmo();

  EXPECT_THAT(inspect_deprecated::ReadFromFidlObject(result.take_value()),
              NodeMatches(AllOf(NameMatches(kFrameStatsNodeName), MetricList(IsEmpty()),
                                PropertyList(SizeIs(1)))));
}
TEST_F(FrameStatsTest, SmokeTest_DummyFrameTimings) {
  FrameStats stats(root_object_.CreateChild(kFrameStatsNodeName), nullptr);

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
  for (int i = 0; i < 15; i++) {
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
  for (int i = 0; i < 15; i++) {
    stats.RecordFrame(delayed_times, vsync_interval);

    delayed_times.latch_point_time = delayed_times.actual_presentation_time + zx::msec(1);
    delayed_times.update_done_time = delayed_times.actual_presentation_time + zx::msec(4);
    delayed_times.render_start_time = delayed_times.actual_presentation_time + zx::msec(4);
    delayed_times.render_done_time = delayed_times.actual_presentation_time + zx::msec(20);
    delayed_times.target_presentation_time = delayed_times.actual_presentation_time + zx::msec(16);
    delayed_times.actual_presentation_time += zx::msec(32);
  }

  fit::result<fuchsia::inspect::deprecated::Object> result = ReadInspectVmo();

  EXPECT_THAT(inspect_deprecated::ReadFromFidlObject(result.take_value()),
              NodeMatches(AllOf(NameMatches(kFrameStatsNodeName), MetricList(IsEmpty()),
                                PropertyList(SizeIs(1)))));
}

class FrameStatsCobaltTest : public gtest::TestLoopFixture {
 public:
  static constexpr char kObjectsName[] = "objects";
  FrameStatsCobaltTest()
      : object_(component::Object::Make(kObjectsName)),
        root_object_(component::ObjectDir(object_)) {}

 protected:
  std::shared_ptr<component::Object> object_;
  inspect_deprecated::Node root_object_;
};

TEST_F(FrameStatsCobaltTest, LogFrameTimes) {
  cobalt::CallCountMap cobalt_call_counts;
  FrameStats stats(root_object_.CreateChild(kFrameStatsNodeName),
                   std::make_unique<cobalt::MockCobaltLogger>(&cobalt_call_counts));

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
