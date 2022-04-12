// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/function.h>
#include <lib/fpromise/single_threaded_executor.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/inspect/testing/cpp/inspect.h>
#include <lib/zx/status.h>

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

}  // namespace

// Validate basic usage of OperationTracker using `OperationTracker::Track`.
TEST(VfsInspectOperationTracker, ValidateLayout) {
  inspect::Inspector inspector;
  OperationTracker tracker(inspector.GetRoot(), kOperationName);
  // There should now be a node called "my_operation" with some properties.
  inspect::Hierarchy snapshot = TakeSnapshot(inspector);
  const inspect::Hierarchy* my_operation = snapshot.GetByPath({kOperationName});
  ASSERT_NE(my_operation, nullptr);

  // We should have properties for total/ok/errored operation counts and a latency histogram.
  EXPECT_THAT(*my_operation, NodeMatches(PropertyList(IsSupersetOf({
                                 UintIs(OperationTracker::kOkCountName, 0),
                                 UintIs(OperationTracker::kFailCountName, 0),
                                 UintIs(OperationTracker::kTotalCountName, 0),
                             }))));
  // The Inspect reader doesn't support histograms, so we just check that the property exists.
  EXPECT_TRUE(std::any_of(my_operation->node().properties().cbegin(),
                          my_operation->node().properties().cend(),
                          [](const inspect::PropertyValue& property) {
                            return property.name() == OperationTracker::kLatencyHistogramName;
                          }));
  // Error node should not be present until we record at least one error.
  EXPECT_EQ(snapshot.GetByPath({kOperationName, OperationTracker::kErrorNodeName}), nullptr);

  // Now we record some operations. A new error node should be created when an error is encountered.
  tracker.Track([] { return ZX_OK; });
  tracker.Track([] { return ZX_ERR_IO; });
  tracker.Track([] { return ZX_ERR_ACCESS_DENIED; });
  tracker.Track([] { return ZX_ERR_ACCESS_DENIED; });

  snapshot = TakeSnapshot(inspector);
  my_operation = snapshot.GetByPath({kOperationName});
  ASSERT_NE(my_operation, nullptr);
  const inspect::Hierarchy* error_node =
      my_operation->GetByPath({OperationTracker::kErrorNodeName});
  ASSERT_NE(error_node, nullptr);

  // Validate operation counts.
  EXPECT_THAT(*my_operation, NodeMatches(PropertyList(IsSupersetOf({
                                 UintIs(OperationTracker::kOkCountName, 1),
                                 UintIs(OperationTracker::kFailCountName, 3),
                                 UintIs(OperationTracker::kTotalCountName, 4),
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
  OperationTracker tracker(inspector.GetRoot(), kOperationName);

  {
    // No events should be recorded until they go out of scope. We also check that we can move an
    // event before/after setting the status without affecting the result.
    OperationTracker::TrackerEvent fail_event = tracker.NewEvent();
    OperationTracker::TrackerEvent fail_event_moved = std::move(fail_event);
    fail_event_moved.SetStatus(ZX_ERR_IO);
    OperationTracker::TrackerEvent ok_event = tracker.NewEvent();
    ok_event.SetStatus(ZX_OK);
    OperationTracker::TrackerEvent ok_event_moved = std::move(ok_event);
    inspect::Hierarchy snapshot = TakeSnapshot(inspector);
    const inspect::Hierarchy* my_operation = snapshot.GetByPath({kOperationName});
    ASSERT_NE(my_operation, nullptr);
    EXPECT_THAT(*my_operation, NodeMatches(PropertyList(IsSupersetOf({
                                   UintIs(OperationTracker::kOkCountName, 0),
                                   UintIs(OperationTracker::kFailCountName, 0),
                                   UintIs(OperationTracker::kTotalCountName, 0),
                               }))));
  }

  inspect::Hierarchy snapshot = TakeSnapshot(inspector);
  const inspect::Hierarchy* my_operation = snapshot.GetByPath({kOperationName});
  ASSERT_NE(my_operation, nullptr);
  const inspect::Hierarchy* error_node =
      my_operation->GetByPath({OperationTracker::kErrorNodeName});
  ASSERT_NE(error_node, nullptr);

  // Validate operation and error counts.
  EXPECT_THAT(*my_operation, NodeMatches(PropertyList(IsSupersetOf({
                                 UintIs(OperationTracker::kOkCountName, 1),
                                 UintIs(OperationTracker::kFailCountName, 1),
                                 UintIs(OperationTracker::kTotalCountName, 2),
                             }))));
  EXPECT_THAT(*error_node, NodeMatches(PropertyList(IsSupersetOf({
                               UintIs(zx_status_get_string(ZX_ERR_IO), 1),
                           }))));
}

TEST(VfsInspectOperationTracker, LatencyEventThreaded) {
  inspect::Inspector inspector;
  OperationTracker tracker(inspector.GetRoot(), kOperationName);

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
      my_operation->GetByPath({OperationTracker::kErrorNodeName});
  ASSERT_NE(error_node, nullptr);

  // Validate operation and error counts.
  EXPECT_THAT(*my_operation, NodeMatches(PropertyList(IsSupersetOf({
                                 UintIs(OperationTracker::kOkCountName, 1),
                                 UintIs(OperationTracker::kFailCountName, 1),
                                 UintIs(OperationTracker::kTotalCountName, 2),
                             }))));
  EXPECT_THAT(*error_node, NodeMatches(PropertyList(IsSupersetOf({
                               UintIs(zx_status_get_string(ZX_ERR_IO), 1),
                           }))));
}

}  // namespace fs_inspect
