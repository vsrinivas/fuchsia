// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/function.h>
#include <lib/fpromise/single_threaded_executor.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/inspect/testing/cpp/inspect.h>
#include <lib/zx/result.h>

#include <condition_variable>
#include <mutex>
#include <thread>

#include <gtest/gtest.h>

#include "src/lib/storage/vfs/cpp/inspect/operation_tracker.h"

using namespace ::testing;
using namespace inspect::testing;

namespace fs_inspect {

namespace {

constexpr char kOperationName[] = "my_operation";

// Take a snapshot of a given Inspector's state.
inspect::Hierarchy TakeSnapshot(inspect::Inspector& inspector) {
  auto snapshot = fpromise::run_single_threaded(inspect::ReadFromInspector(inspector));
  ZX_ASSERT(snapshot.is_ok());
  return std::move(snapshot.value());
}

constexpr LatencyHistogramSettings kHistogramSettings{
    .time_base = zx::usec(1),
    .floor = 0,
    .initial_step = 5,
    .step_multiplier = 2,
    .buckets = 16,
};

}  // namespace

// Validate basic usage of OperationTracker using `OperationTracker::Track`.
TEST(VfsInspectOperationTracker, ValidateLayout) {
  inspect::Inspector inspector;
  OperationTrackerFuchsia tracker(inspector.GetRoot(), kOperationName, kHistogramSettings);
  // There should now be a node called "my_operation" with some properties.
  inspect::Hierarchy snapshot = TakeSnapshot(inspector);
  const inspect::Hierarchy* my_operation = snapshot.GetByPath({kOperationName});
  ASSERT_NE(my_operation, nullptr);

  // We should have properties for total/ok/errored operation counts and a latency histogram.
  EXPECT_THAT(*my_operation, NodeMatches(PropertyList(IsSupersetOf({
                                 UintIs(OperationTrackerFuchsia::kOkCountName, 0),
                                 UintIs(OperationTrackerFuchsia::kFailCountName, 0),
                                 UintIs(OperationTrackerFuchsia::kTotalCountName, 0),
                             }))));
  // The Inspect reader doesn't support histograms, so we just check that the property exists.
  EXPECT_TRUE(std::any_of(
      my_operation->node().properties().cbegin(), my_operation->node().properties().cend(),
      [](const inspect::PropertyValue& property) {
        return property.name() == OperationTrackerFuchsia::kLatencyHistogramName;
      }));
  // Error node should not be present until we record at least one error.
  EXPECT_EQ(snapshot.GetByPath({kOperationName, OperationTrackerFuchsia::kErrorNodeName}), nullptr);

  // Now we record some operations. A new error node should be created when an error is encountered.
  tracker.Track([] { return ZX_OK; });
  tracker.Track([] { return ZX_ERR_IO; });
  tracker.Track([] { return ZX_ERR_ACCESS_DENIED; });
  tracker.Track([] { return ZX_ERR_ACCESS_DENIED; });

  snapshot = TakeSnapshot(inspector);
  my_operation = snapshot.GetByPath({kOperationName});
  ASSERT_NE(my_operation, nullptr);
  const inspect::Hierarchy* error_node =
      my_operation->GetByPath({OperationTrackerFuchsia::kErrorNodeName});
  ASSERT_NE(error_node, nullptr);

  // Validate operation counts.
  EXPECT_THAT(*my_operation, NodeMatches(PropertyList(IsSupersetOf({
                                 UintIs(OperationTrackerFuchsia::kOkCountName, 1),
                                 UintIs(OperationTrackerFuchsia::kFailCountName, 3),
                                 UintIs(OperationTrackerFuchsia::kTotalCountName, 4),
                             }))));
  // Validate error counts.
  EXPECT_THAT(*error_node, NodeMatches(PropertyList(IsSupersetOf({
                               UintIs(zx_status_get_string(ZX_ERR_IO), 1),
                               UintIs(zx_status_get_string(ZX_ERR_ACCESS_DENIED), 2),
                           }))));
}

// Validate behavior `OperationTracker::NewEvent` in a single-threaded context.
TEST(VfsInspectOperationTracker, LatencyEvent) {
  inspect::Inspector inspector;
  OperationTrackerFuchsia tracker(inspector.GetRoot(), kOperationName, kHistogramSettings);

  {
    // No events should be recorded until they go out of scope. We also check that we can move an
    // event before/after setting the status without affecting the result.
    OperationTrackerFuchsia::TrackerEvent fail_event = tracker.NewEvent();
    OperationTrackerFuchsia::TrackerEvent fail_event_moved = std::move(fail_event);
    fail_event_moved.SetStatus(ZX_ERR_IO);
    OperationTrackerFuchsia::TrackerEvent ok_event = tracker.NewEvent();
    ok_event.SetStatus(ZX_OK);
    OperationTrackerFuchsia::TrackerEvent ok_event_moved = std::move(ok_event);
    inspect::Hierarchy snapshot = TakeSnapshot(inspector);
    const inspect::Hierarchy* my_operation = snapshot.GetByPath({kOperationName});
    ASSERT_NE(my_operation, nullptr);
    EXPECT_THAT(*my_operation, NodeMatches(PropertyList(IsSupersetOf({
                                   UintIs(OperationTrackerFuchsia::kOkCountName, 0),
                                   UintIs(OperationTrackerFuchsia::kFailCountName, 0),
                                   UintIs(OperationTrackerFuchsia::kTotalCountName, 0),
                               }))));
  }

  inspect::Hierarchy snapshot = TakeSnapshot(inspector);
  const inspect::Hierarchy* my_operation = snapshot.GetByPath({kOperationName});
  ASSERT_NE(my_operation, nullptr);
  const inspect::Hierarchy* error_node =
      my_operation->GetByPath({OperationTrackerFuchsia::kErrorNodeName});
  ASSERT_NE(error_node, nullptr);

  // Validate operation and error counts.
  EXPECT_THAT(*my_operation, NodeMatches(PropertyList(IsSupersetOf({
                                 UintIs(OperationTrackerFuchsia::kOkCountName, 1),
                                 UintIs(OperationTrackerFuchsia::kFailCountName, 1),
                                 UintIs(OperationTrackerFuchsia::kTotalCountName, 2),
                             }))));
  EXPECT_THAT(*error_node, NodeMatches(PropertyList(IsSupersetOf({
                               UintIs(zx_status_get_string(ZX_ERR_IO), 1),
                           }))));
}

TEST(VfsInspectOperationTracker, LatencyEventThreaded) {
  inspect::Inspector inspector;
  OperationTrackerFuchsia tracker(inspector.GetRoot(), kOperationName, kHistogramSettings);

  // Record an event from a different thread by passing a callback we create in this thread.
  std::mutex mutex;
  std::condition_variable cv;
  fit::function<void()> worker_callback = nullptr;

  std::thread worker_thread([&] {
    std::unique_lock lock(mutex);
    cv.wait(lock, [&] { return worker_callback != nullptr; });
    worker_callback();
    // We have to explicitly delete the callback so the lambda captures go out of scope.
    worker_callback = nullptr;
  });

  {
    std::unique_lock lock(mutex);
    OperationTracker::TrackerEvent event_one = tracker.NewEvent();
    OperationTracker::TrackerEvent event_two = tracker.NewEvent();
    worker_callback = [event_one = std::move(event_one),
                       event_two = std::move(event_two)]() mutable {
      event_one.SetStatus(ZX_OK);
      event_two.SetStatus(ZX_ERR_IO);
    };
  }

  cv.notify_one();
  worker_thread.join();

  inspect::Hierarchy snapshot = TakeSnapshot(inspector);
  const inspect::Hierarchy* my_operation = snapshot.GetByPath({kOperationName});
  ASSERT_NE(my_operation, nullptr);
  const inspect::Hierarchy* error_node =
      my_operation->GetByPath({OperationTrackerFuchsia::kErrorNodeName});
  ASSERT_NE(error_node, nullptr);

  // Validate operation and error counts.
  EXPECT_THAT(*my_operation, NodeMatches(PropertyList(IsSupersetOf({
                                 UintIs(OperationTrackerFuchsia::kOkCountName, 1),
                                 UintIs(OperationTrackerFuchsia::kFailCountName, 1),
                                 UintIs(OperationTrackerFuchsia::kTotalCountName, 2),
                             }))));
  EXPECT_THAT(*error_node, NodeMatches(PropertyList(IsSupersetOf({
                               UintIs(zx_status_get_string(ZX_ERR_IO), 1),
                           }))));
}

}  // namespace fs_inspect
