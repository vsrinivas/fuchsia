// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <string>
#include <thread>

#include <lib/fit/barrier.h>
#include <lib/fit/sequencer.h>
#include <lib/fit/single_threaded_executor.h>
#include <unittest/unittest.h>

namespace {

// Wrapping tasks with a barrier should still allow them to complete, even without a sync.
bool wrapping_tasks_no_sync() {
  BEGIN_TEST;

  bool array[3] = {};
  auto a = fit::make_promise([&] { array[0] = true; });
  auto b = fit::make_promise([&] { array[1] = true; });
  auto c = fit::make_promise([&] { array[2] = true; });

  for (size_t i = 0; i < std::size(array); i++) {
    EXPECT_FALSE(array[i]);
  }

  fit::barrier barrier;

  fit::single_threaded_executor executor;
  executor.schedule_task(a.wrap_with(barrier));
  executor.schedule_task(b.wrap_with(barrier));
  executor.schedule_task(c.wrap_with(barrier));
  executor.run();

  for (size_t i = 0; i < std::size(array); i++) {
    EXPECT_TRUE(array[i]);
  }

  END_TEST;
}

// Syncing tasks with should still allow them to complete, even without pending work.
bool sync_no_wrapped_tasks() {
  BEGIN_TEST;

  bool array[3] = {};
  auto a = fit::make_promise([&] { array[0] = true; });
  auto b = fit::make_promise([&] { array[1] = true; });
  auto c = fit::make_promise([&] { array[2] = true; });

  for (size_t i = 0; i < std::size(array); i++) {
    EXPECT_FALSE(array[i]);
  }

  fit::barrier barrier;

  fit::single_threaded_executor executor;
  executor.schedule_task(barrier.sync().and_then(std::move(a)));
  executor.schedule_task(barrier.sync().and_then(std::move(b)));
  executor.schedule_task(barrier.sync().and_then(std::move(c)));
  executor.run();

  for (size_t i = 0; i < std::size(array); i++) {
    EXPECT_TRUE(array[i]);
  }

  END_TEST;
}

// Wrap up a bunch of work in the barrier before syncing a barrier.
// Observe that the wrapped work completes before the sync.
bool wrap_then_sync() {
  BEGIN_TEST;

  bool array[3] = {};
  auto a = fit::make_promise([&] { array[0] = true; });
  auto b = fit::make_promise([&] { array[1] = true; });
  auto c = fit::make_promise([&] { array[2] = true; });

  bool sync_complete = false;
  auto sync_promise = fit::make_promise([&] {
    for (size_t i = 0; i < std::size(array); i++) {
      EXPECT_TRUE(array[i]);
    }
    sync_complete = true;
  });

  for (size_t i = 0; i < std::size(array); i++) {
    EXPECT_FALSE(array[i]);
  }

  fit::barrier barrier;
  auto a_tracked = a.wrap_with(barrier);
  auto b_tracked = b.wrap_with(barrier);
  auto c_tracked = c.wrap_with(barrier);

  // Note that we schedule the "sync" task first, even though we expect
  // it to actually be executed last. This is just a little extra nudge to ensure
  // our executor isn't implicitly supplying this order for us.
  fit::single_threaded_executor executor;
  executor.schedule_task(barrier.sync().and_then(std::move(sync_promise)));
  executor.schedule_task(std::move(a_tracked));
  executor.schedule_task(std::move(b_tracked));
  executor.schedule_task(std::move(c_tracked));
  executor.run();

  EXPECT_TRUE(sync_complete);

  END_TEST;
}

// Observe that the order of "barrier.wrap" does not re-order the wrapped promises, but
// merely provides ordering before the sync point.
bool wrap_preserves_initial_order() {
  BEGIN_TEST;

  // Create three promises.
  //
  // They will be sequencer-wrapped in the order "a, b, c".
  // They will be barrier-wrapped in the order "c, b, a".
  //
  // Observe that by wrapping them, the sequence order is still preserved.
  bool array[3] = {};
  auto a = fit::make_promise([&] {
    array[0] = true;
    assert(!array[1]);
    assert(!array[2]);
  });
  auto b = fit::make_promise([&] {
    assert(array[0]);
    array[1] = true;
    assert(!array[2]);
  });
  auto c = fit::make_promise([&] {
    assert(array[0]);
    assert(array[1]);
    array[2] = true;
  });

  bool sync_complete = false;
  auto sync_promise = fit::make_promise([&] {
    for (size_t i = 0; i < std::size(array); i++) {
      EXPECT_TRUE(array[i]);
    }
    sync_complete = true;
  });

  for (size_t i = 0; i < std::size(array); i++) {
    EXPECT_FALSE(array[i]);
  }

  fit::sequencer seq;
  auto a_sequenced = a.wrap_with(seq);
  auto b_sequenced = b.wrap_with(seq);
  auto c_sequenced = c.wrap_with(seq);

  fit::barrier barrier;
  auto c_tracked = c_sequenced.wrap_with(barrier);
  auto b_tracked = b_sequenced.wrap_with(barrier);
  auto a_tracked = a_sequenced.wrap_with(barrier);

  fit::single_threaded_executor executor;
  executor.schedule_task(barrier.sync().and_then(std::move(sync_promise)));
  executor.schedule_task(std::move(a_tracked));
  executor.schedule_task(std::move(b_tracked));
  executor.schedule_task(std::move(c_tracked));
  executor.run();

  EXPECT_TRUE(sync_complete);

  END_TEST;
}

// Observe that promises chained after the "wrap" request do not block the sync.
bool work_after_wrap_non_blocking() {
  BEGIN_TEST;

  bool work_complete = false;
  auto work = fit::make_promise([&] { work_complete = true; });

  bool sync_complete = false;
  auto sync_promise = fit::make_promise([&] {
    assert(work_complete);
    sync_complete = true;
  });

  fit::barrier barrier;
  auto work_wrapped = barrier.wrap(std::move(work))
                          .then([&](fit::context& context, fit::result<>&) -> fit::result<> {
                            // If the full chain of execution after "work" was required to complete
                            // before sync, then "sync_complete" will remain false forever, and this
                            // task will never be completed.
                            if (!sync_complete) {
                              context.suspend_task().resume_task();
                              return fit::pending();
                            }
                            return fit::ok();
                          });

  fit::single_threaded_executor executor;
  executor.schedule_task(std::move(work_wrapped));
  executor.schedule_task(barrier.sync().and_then(std::move(sync_promise)));
  executor.run();

  EXPECT_TRUE(work_complete);
  EXPECT_TRUE(sync_complete);

  END_TEST;
}

// Observe that back-to-back sync operations are still ordered, and cannot
// skip ahead of previously wrapped work.
bool multiple_syncs_after_work_are_ordered() {
  BEGIN_TEST;
  bool work_complete = false;
  auto work = fit::make_promise([&] { work_complete = true; });

  bool syncs_complete[] = {false, false};
  fit::promise<void, void> sync_promises[] = {
      fit::make_promise([&] {
        assert(work_complete);
        assert(!syncs_complete[1]);
        syncs_complete[0] = true;
      }),
      fit::make_promise([&] {
        assert(work_complete);
        assert(syncs_complete[0]);
        syncs_complete[1] = true;
      }),
  };

  fit::barrier barrier;
  auto work_wrapped = work.wrap_with(barrier);

  fit::single_threaded_executor executor;
  executor.schedule_task(barrier.sync().and_then(std::move(sync_promises[0])));
  executor.schedule_task(barrier.sync().and_then(std::move(sync_promises[1])));
  executor.schedule_task(std::move(work_wrapped));
  executor.run();

  EXPECT_TRUE(work_complete);
  EXPECT_TRUE(syncs_complete[0]);
  EXPECT_TRUE(syncs_complete[1]);
  END_TEST;
}

// Abandoning promises should still allow sync to complete.
bool abandoned_promises_are_ordered_by_sync() {
  BEGIN_TEST;

  auto work = fit::make_promise([&] { assert(false); });

  bool sync_complete = false;
  auto sync_promise = fit::make_promise([&] { sync_complete = true; });

  fit::barrier barrier;
  fit::single_threaded_executor executor;
  {
    auto work_wrapped = work.wrap_with(barrier);
    executor.schedule_task(barrier.sync().and_then(std::move(sync_promise)));

    // "work_wrapped" is destroyed (abandoned) here.
  }
  executor.run();

  EXPECT_TRUE(sync_complete);

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(barrier_tests)
RUN_TEST(wrapping_tasks_no_sync)
RUN_TEST(sync_no_wrapped_tasks)
RUN_TEST(wrap_then_sync)
RUN_TEST(wrap_preserves_initial_order)
RUN_TEST(work_after_wrap_non_blocking)
RUN_TEST(multiple_syncs_after_work_are_ordered)
RUN_TEST(abandoned_promises_are_ordered_by_sync)
END_TEST_CASE(barrier_tests)
