// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fpromise/barrier.h>
#include <lib/fpromise/sequencer.h>
#include <lib/fpromise/single_threaded_executor.h>
#include <unistd.h>

#include <string>
#include <thread>

#include <zxtest/zxtest.h>

namespace {

// Wrapping tasks with a barrier should still allow them to complete, even without a sync.
TEST(BarrierTests, wrapping_tasks_no_sync) {
  bool array[3] = {};
  auto a = fpromise::make_promise([&] { array[0] = true; });
  auto b = fpromise::make_promise([&] { array[1] = true; });
  auto c = fpromise::make_promise([&] { array[2] = true; });

  for (size_t i = 0; i < std::size(array); i++) {
    EXPECT_FALSE(array[i]);
  }

  fpromise::barrier barrier;

  fpromise::single_threaded_executor executor;
  executor.schedule_task(a.wrap_with(barrier));
  executor.schedule_task(b.wrap_with(barrier));
  executor.schedule_task(c.wrap_with(barrier));
  executor.run();

  for (size_t i = 0; i < std::size(array); i++) {
    EXPECT_TRUE(array[i]);
  }
}

// Syncing tasks with should still allow them to complete, even without pending work.
TEST(BarrierTests, sync_no_wrapped_tasks) {
  bool array[3] = {};
  auto a = fpromise::make_promise([&] { array[0] = true; });
  auto b = fpromise::make_promise([&] { array[1] = true; });
  auto c = fpromise::make_promise([&] { array[2] = true; });

  for (size_t i = 0; i < std::size(array); i++) {
    EXPECT_FALSE(array[i]);
  }

  fpromise::barrier barrier;

  fpromise::single_threaded_executor executor;
  executor.schedule_task(barrier.sync().and_then(std::move(a)));
  executor.schedule_task(barrier.sync().and_then(std::move(b)));
  executor.schedule_task(barrier.sync().and_then(std::move(c)));
  executor.run();

  for (size_t i = 0; i < std::size(array); i++) {
    EXPECT_TRUE(array[i]);
  }
}

// Wrap up a bunch of work in the barrier before syncing a barrier.
// Observe that the wrapped work completes before the sync.
TEST(BarrierTests, wrap_then_sync) {
  bool array[3] = {};
  auto a = fpromise::make_promise([&] { array[0] = true; });
  auto b = fpromise::make_promise([&] { array[1] = true; });
  auto c = fpromise::make_promise([&] { array[2] = true; });

  bool sync_complete = false;
  auto sync_promise = fpromise::make_promise([&] {
    for (size_t i = 0; i < std::size(array); i++) {
      EXPECT_TRUE(array[i]);
    }
    sync_complete = true;
  });

  for (size_t i = 0; i < std::size(array); i++) {
    EXPECT_FALSE(array[i]);
  }

  fpromise::barrier barrier;
  auto a_tracked = a.wrap_with(barrier);
  auto b_tracked = b.wrap_with(barrier);
  auto c_tracked = c.wrap_with(barrier);

  // Note that we schedule the "sync" task first, even though we expect
  // it to actually be executed last. This is just a little extra nudge to ensure
  // our executor isn't implicitly supplying this order for us.
  fpromise::single_threaded_executor executor;
  executor.schedule_task(barrier.sync().and_then(std::move(sync_promise)));
  executor.schedule_task(std::move(a_tracked));
  executor.schedule_task(std::move(b_tracked));
  executor.schedule_task(std::move(c_tracked));
  executor.run();

  EXPECT_TRUE(sync_complete);
}

// Observe that the order of "barrier.wrap" does not re-order the wrapped promises, but
// merely provides ordering before the sync point.
TEST(BarrierTests, wrap_preserves_initial_order) {
  // Create three promises.
  //
  // They will be sequencer-wrapped in the order "a, b, c".
  // They will be barrier-wrapped in the order "c, b, a".
  //
  // Observe that by wrapping them, the sequence order is still preserved.
  bool array[3] = {};
  auto a = fpromise::make_promise([&] {
    array[0] = true;
    assert(!array[1]);
    assert(!array[2]);
  });
  auto b = fpromise::make_promise([&] {
    assert(array[0]);
    array[1] = true;
    assert(!array[2]);
  });
  auto c = fpromise::make_promise([&] {
    assert(array[0]);
    assert(array[1]);
    array[2] = true;
  });

  bool sync_complete = false;
  auto sync_promise = fpromise::make_promise([&] {
    for (size_t i = 0; i < std::size(array); i++) {
      EXPECT_TRUE(array[i]);
    }
    sync_complete = true;
  });

  for (size_t i = 0; i < std::size(array); i++) {
    EXPECT_FALSE(array[i]);
  }

  fpromise::sequencer seq;
  auto a_sequenced = a.wrap_with(seq);
  auto b_sequenced = b.wrap_with(seq);
  auto c_sequenced = c.wrap_with(seq);

  fpromise::barrier barrier;
  auto c_tracked = c_sequenced.wrap_with(barrier);
  auto b_tracked = b_sequenced.wrap_with(barrier);
  auto a_tracked = a_sequenced.wrap_with(barrier);

  fpromise::single_threaded_executor executor;
  executor.schedule_task(barrier.sync().and_then(std::move(sync_promise)));
  executor.schedule_task(std::move(a_tracked));
  executor.schedule_task(std::move(b_tracked));
  executor.schedule_task(std::move(c_tracked));
  executor.run();

  EXPECT_TRUE(sync_complete);
}

// Observe that promises chained after the "wrap" request do not block the sync.
TEST(BarrierTests, work_after_wrap_non_blocking) {
  bool work_complete = false;
  auto work = fpromise::make_promise([&] { work_complete = true; });

  bool sync_complete = false;
  auto sync_promise = fpromise::make_promise([&] {
    assert(work_complete);
    sync_complete = true;
  });

  fpromise::barrier barrier;
  auto work_wrapped =
      barrier.wrap(std::move(work))
          .then([&](fpromise::context& context, fpromise::result<>&) -> fpromise::result<> {
            // If the full chain of execution after "work" was required to complete
            // before sync, then "sync_complete" will remain false forever, and this
            // task will never be completed.
            if (!sync_complete) {
              context.suspend_task().resume_task();
              return fpromise::pending();
            }
            return fpromise::ok();
          });

  fpromise::single_threaded_executor executor;
  executor.schedule_task(std::move(work_wrapped));
  executor.schedule_task(barrier.sync().and_then(std::move(sync_promise)));
  executor.run();

  EXPECT_TRUE(work_complete);
  EXPECT_TRUE(sync_complete);
}

// Observe that back-to-back sync operations are still ordered, and cannot
// skip ahead of previously wrapped work.
TEST(BarrierTests, multiple_syncs_after_work_are_ordered) {
  bool work_complete = false;
  auto work = fpromise::make_promise([&] { work_complete = true; });

  bool syncs_complete[] = {false, false};
  fpromise::promise<void, void> sync_promises[] = {
      fpromise::make_promise([&] {
        assert(work_complete);
        assert(!syncs_complete[1]);
        syncs_complete[0] = true;
      }),
      fpromise::make_promise([&] {
        assert(work_complete);
        assert(syncs_complete[0]);
        syncs_complete[1] = true;
      }),
  };

  fpromise::barrier barrier;
  auto work_wrapped = work.wrap_with(barrier);

  fpromise::single_threaded_executor executor;
  executor.schedule_task(barrier.sync().and_then(std::move(sync_promises[0])));
  executor.schedule_task(barrier.sync().and_then(std::move(sync_promises[1])));
  executor.schedule_task(std::move(work_wrapped));
  executor.run();

  EXPECT_TRUE(work_complete);
  EXPECT_TRUE(syncs_complete[0]);
  EXPECT_TRUE(syncs_complete[1]);
}

// Abandoning promises should still allow sync to complete.
TEST(BarrierTests, abandoned_promises_are_ordered_by_sync) {
  auto work = fpromise::make_promise([&] { assert(false); });

  bool sync_complete = false;
  auto sync_promise = fpromise::make_promise([&] { sync_complete = true; });

  fpromise::barrier barrier;
  fpromise::single_threaded_executor executor;
  {
    auto work_wrapped = work.wrap_with(barrier);
    executor.schedule_task(barrier.sync().and_then(std::move(sync_promise)));

    // "work_wrapped" is destroyed (abandoned) here.
  }
  executor.run();

  EXPECT_TRUE(sync_complete);
}

}  // namespace
