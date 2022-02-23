// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fasync/barrier.h>
#include <lib/fasync/sequencer.h>
#include <lib/fasync/single_threaded_executor.h>
#include <unistd.h>

#include <string>
#include <thread>

#include <zxtest/zxtest.h>

namespace {

// Wrapping tasks with a barrier should still allow them to complete, even without a sync.
TEST(BarrierTests, wrapping_tasks_no_sync) {
  bool array[3] = {};
  auto a = fasync::make_future([&] { array[0] = true; });
  auto b = fasync::make_future([&] { array[1] = true; });
  auto c = fasync::make_future([&] { array[2] = true; });

  for (bool b : array) {
    EXPECT_FALSE(b);
  }

  fasync::barrier barrier;

  fasync::single_threaded_executor executor;
  executor.schedule(std::move(a) | fasync::wrap_with(barrier));
  executor.schedule(std::move(b) | fasync::wrap_with(barrier));
  executor.schedule(std::move(c) | fasync::wrap_with(barrier));
  executor.run();

  for (bool b : array) {
    EXPECT_TRUE(b);
  }
}

// Syncing tasks with should still allow them to complete, even without pending work.
TEST(BarrierTests, sync_no_wrapped_tasks) {
  bool array[3] = {};
  auto a = [&] { array[0] = true; };
  auto b = [&] { array[1] = true; };
  auto c = [&] { array[2] = true; };

  for (bool b : array) {
    EXPECT_FALSE(b);
  }

  fasync::barrier barrier;

  fasync::single_threaded_executor executor;
  executor.schedule(barrier.sync() | fasync::and_then(std::move(a)));
  executor.schedule(barrier.sync() | fasync::and_then(std::move(b)));
  executor.schedule(barrier.sync() | fasync::and_then(std::move(c)));
  executor.run();

  for (bool b : array) {
    EXPECT_TRUE(b);
  }
}

// Wrap up a bunch of work in the barrier before syncing a barrier.
// Observe that the wrapped work completes before the sync.
TEST(BarrierTests, wrap_then_sync) {
  bool array[3] = {};
  auto a = fasync::make_future([&] { array[0] = true; });
  auto b = fasync::make_future([&] { array[1] = true; });
  auto c = fasync::make_future([&] { array[2] = true; });

  bool sync_complete = false;
  auto sync = [&] {
    for (size_t i = 0; i < std::size(array); i++) {
      EXPECT_TRUE(array[i]);
    }
    sync_complete = true;
  };

  for (size_t i = 0; i < std::size(array); i++) {
    EXPECT_FALSE(array[i]);
  }

  fasync::barrier barrier;
  auto a_tracked = std::move(a) | fasync::wrap_with(barrier);
  auto b_tracked = std::move(b) | fasync::wrap_with(barrier);
  auto c_tracked = std::move(c) | fasync::wrap_with(barrier);

  // Note that we schedule the "sync" task first, even though we expect it to actually be executed
  // last. This is just a little extra nudge to ensure our executor isn't implicitly supplying this
  // order for us.
  fasync::single_threaded_executor executor;
  executor.schedule(barrier.sync() | fasync::and_then(std::move(sync)));
  executor.schedule(std::move(a_tracked));
  executor.schedule(std::move(b_tracked));
  executor.schedule(std::move(c_tracked));
  executor.run();

  EXPECT_TRUE(sync_complete);
}

// Observe that the order of "barrier.wrap" does not re-order the wrapped futures, but merely
// provides ordering before the sync point.
TEST(BarrierTests, wrap_preserves_initial_order) {
  // Create three futures.
  //
  // They will be sequencer-wrapped in the order "a, b, c".
  // They will be barrier-wrapped in the order "c, b, a".
  //
  // Observe that by wrapping them, the sequence order is still preserved.
  bool array[3] = {};
  auto a = fasync::make_future([&] {
    array[0] = true;
    assert(!array[1]);
    assert(!array[2]);
  });
  auto b = fasync::make_future([&] {
    assert(array[0]);
    array[1] = true;
    assert(!array[2]);
  });
  auto c = fasync::make_future([&] {
    assert(array[0]);
    assert(array[1]);
    array[2] = true;
  });

  bool sync_complete = false;
  auto sync = [&] {
    for (size_t i = 0; i < std::size(array); i++) {
      EXPECT_TRUE(array[i]);
    }
    sync_complete = true;
  };

  for (size_t i = 0; i < std::size(array); i++) {
    EXPECT_FALSE(array[i]);
  }

  fasync::sequencer seq;
  auto a_sequenced = std::move(a) | fasync::wrap_with(seq);
  auto b_sequenced = std::move(b) | fasync::wrap_with(seq);
  auto c_sequenced = std::move(c) | fasync::wrap_with(seq);

  fasync::barrier barrier;
  auto c_tracked = std::move(c_sequenced) | fasync::wrap_with(barrier);
  auto b_tracked = std::move(b_sequenced) | fasync::wrap_with(barrier);
  auto a_tracked = std::move(a_sequenced) | fasync::wrap_with(barrier);

  fasync::single_threaded_executor executor;
  executor.schedule(barrier.sync() | fasync::and_then(std::move(sync)));
  executor.schedule(std::move(a_tracked));
  executor.schedule(std::move(b_tracked));
  executor.schedule(std::move(c_tracked));
  executor.run();

  EXPECT_TRUE(sync_complete);
}

// Observe that futures chained after the "wrap" request do not block the sync.
TEST(BarrierTests, work_after_wrap_non_blocking) {
  bool work_complete = false;
  auto work = fasync::make_future([&] { work_complete = true; });

  bool sync_complete = false;
  auto sync = [&] {
    assert(work_complete);
    sync_complete = true;
  };

  fasync::barrier barrier;
  auto work_wrapped =
      barrier.wrap(std::move(work)) | fasync::then([&](fasync::context& context) -> fasync::poll<> {
        // If the full chain of execution after "work" was required to complete before sync, then
        // |sync_complete| will remain false forever, and this task will never be completed.
        if (!sync_complete) {
          context.suspend_task().resume();
          return fasync::pending();
        }
        return fasync::ready();
      });

  fasync::single_threaded_executor executor;
  executor.schedule(std::move(work_wrapped));
  executor.schedule(barrier.sync() | fasync::and_then(std::move(sync)));
  executor.run();

  EXPECT_TRUE(work_complete);
  EXPECT_TRUE(sync_complete);
}

// Observe that back-to-back sync operations are still ordered, and cannot skip ahead of previously
// wrapped work.
TEST(BarrierTests, multiple_syncs_after_work_are_ordered) {
  bool work_complete = false;
  auto work = fasync::make_future([&] { work_complete = true; });

  bool syncs_complete[] = {false, false};
  auto sync1 = [&] {
    assert(work_complete);
    assert(!syncs_complete[1]);
    syncs_complete[0] = true;
  };
  auto sync2 = [&] {
    assert(work_complete);
    assert(syncs_complete[0]);
    syncs_complete[1] = true;
  };

  fasync::barrier barrier;
  auto work_wrapped = std::move(work) | fasync::wrap_with(barrier);

  fasync::single_threaded_executor executor;
  executor.schedule(barrier.sync() | fasync::and_then(std::move(sync1)));
  executor.schedule(barrier.sync() | fasync::and_then(std::move(sync2)));
  executor.schedule(std::move(work_wrapped));
  executor.run();

  EXPECT_TRUE(work_complete);
  EXPECT_TRUE(syncs_complete[0]);
  EXPECT_TRUE(syncs_complete[1]);
}

// Abandoning futures should still allow sync to complete.
TEST(BarrierTests, abandoned_futures_are_ordered_by_sync) {
  auto work = fasync::make_future([&] { assert(false); });

  bool sync_complete = false;
  auto sync = [&] { sync_complete = true; };

  fasync::barrier barrier;
  fasync::single_threaded_executor executor;
  {
    auto work_wrapped = std::move(work) | fasync::wrap_with(barrier);
    executor.schedule(barrier.sync() | fasync::and_then(std::move(sync)));

    // |work_wrapped| is destroyed (abandoned) here.
  }
  executor.run();

  EXPECT_TRUE(sync_complete);
}

}  // namespace
