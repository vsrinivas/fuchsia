// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/fit/defer.h>
#include <lib/unittest/unittest.h>
#include <zircon/compiler.h>

#include <kernel/scheduler_state.h>
#include <kernel/thread.h>
#include <ktl/array.h>
#include <ktl/unique_ptr.h>

#include "tests.h"

#include <ktl/enforce.h>

struct WaitQueueOrderingTests {
 public:
  // Note that we disable static thread analysis for this test.  Typically,
  // working with the internal state of threads and wait queues requires holding
  // particular locks at particular times in order to guarantee consistency and
  // proper memory ordering semantics.
  //
  // In these tests, however, are threads and wait queue collections are
  // basically fake.  The threads are only ever partially initialized, and are
  // never run or made available to be scheduled.  Neither the "threads" nor the
  // "wqc" will ever be exposed outside of the test, and since
  // inserting/removing/peeking WQCs never interacts with any global state, it
  // should be fine to disable static analysis for this test.
  static bool Test() __TA_NO_THREAD_SAFETY_ANALYSIS {
    BEGIN_TEST;

    // Set up the things we will need to run our basic tests.  We need a few
    // Thread structures (although we don't need or even want them to ever run),
    // and a WaitQueueCollection (encapsulated by WaitQueue, this is the object
    // which determines the wake order)
    fbl::AllocChecker ac;
    constexpr size_t kThreadCount = 4;
    ktl::array<ktl::unique_ptr<Thread>, kThreadCount> threads;

    for (size_t i = 0; i < ktl::size(threads); ++i) {
      threads[i].reset(new (&ac) Thread{});
      ASSERT_TRUE(ac.check());
    }

    ktl::unique_ptr<WaitQueueCollection> wqc{new (&ac) WaitQueueCollection{}};
    ASSERT_TRUE(ac.check());
    auto cleanup = fit::defer([&wqc]() __TA_NO_THREAD_SAFETY_ANALYSIS {
      while (wqc->Count() > 0) {
        wqc->Remove(wqc->Peek(1));
      }
    });

    // Aliases to reduce the typing just a bit.
    Thread& t0 = *threads[0];
    Thread& t1 = *threads[1];
    Thread& t2 = *threads[2];
    Thread& t3 = *threads[3];

    SchedTime now{ZX_SEC(300)};

    // No one in in the queue right now.  If we Peek it, we should get back
    // nullptr.
    ASSERT_NULL(wqc->Peek(now.raw_value()));

    // Add a fair thread to the collection.  As the only thread in the
    // collection, it should be chosen no matter what.
    ResetFair(t0, kDefaultWeight, now);
    wqc->Insert(&t0);
    ASSERT_EQ(&t0, wqc->Peek(now.raw_value()));

    // Add a higher weight thread with the same start time to the collection.
    // It should be chosen instead of the normal weight thread.
    ResetFair(t1, kHighWeight, now);
    wqc->Insert(&t1);
    ASSERT_EQ(&t1, wqc->Peek(now.raw_value()));

    // Reduce the weight of the thread we just added and try again.  This time,
    // the initial default weight thread should be chosen.
    wqc->Remove(&t1);
    ResetFair(t1, kLowWeight, now);
    wqc->Insert(&t1);
    ASSERT_EQ(&t0, wqc->Peek(now.raw_value()));

    // Add a deadline thread whose absolute deadline is in the future.
    ResetDeadline(t2, kLongDeadline, now);
    wqc->Insert(&t2);
    ASSERT_EQ(&t2, wqc->Peek(now.raw_value()));

    // Add another deadline thread, with a shorter relative deadline, but an
    // absolute deadline also in the future.  This should become the new choice.
    ResetDeadline(t3, kShortDeadline, now);
    wqc->Insert(&t3);
    ASSERT_EQ(&t3, wqc->Peek(now.raw_value()));

    // Advance time so that we have passed t3's deadline, but not t2's.  t3's
    // absolute deadline is not in the past and t2's is not, so t2 should be
    // chosen over t3.
    now += kShortDeadline + SchedNs(1);
    ASSERT_EQ(&t2, wqc->Peek(now.raw_value()));

    // Now, move past both of the absolute deadlines.  t3 should go back to
    // becoming the proper choice as it has the shorter relative deadline.
    now += kLongDeadline;
    ASSERT_EQ(&t3, wqc->Peek(now.raw_value()));

    // Finally, unwind by "unblocking" all of the threads from the queue and
    // making sure that the come out in the order we expect.  Right now, that
    // should be t3 first, then t2, t0, and finally t1.
    ktl::array expected_order{&t3, &t2, &t0, &t1};
    for (Thread* t : expected_order) {
      ASSERT_EQ(t, wqc->Peek(now.raw_value()));
      wqc->Remove(t);
    }

    // And the queue should finally be empty now.
    ASSERT_NULL(wqc->Peek(now.raw_value()));

    END_TEST;
  }

 private:
  static constexpr SchedWeight kLowWeight = SchedWeight{10};
  static constexpr SchedWeight kDefaultWeight = SchedWeight{20};
  static constexpr SchedWeight kHighWeight = SchedWeight{40};

  static constexpr SchedDuration kShortDeadline = SchedUs(500);
  static constexpr SchedDuration kLongDeadline = kShortDeadline * 10;

  static void ResetFair(Thread& t, SchedWeight weight,
                        SchedTime start_time) __TA_NO_THREAD_SAFETY_ANALYSIS {
    SchedulerState& ss = t.scheduler_state();

    ss.fair_.weight = weight;
    ss.start_time_ = start_time;
    ss.discipline_ = SchedDiscipline::Fair;

    // The initial time slice, NSTR, and the virtual finish time are all
    // meaningless for a thread which is currently blocked. Just default them to
    // 0 for now.
    ss.fair_.initial_time_slice_ns = SchedDuration{0};
    ss.fair_.normalized_timeslice_remainder = SchedRemainder{0};
    ss.finish_time_ = SchedTime{0};
  }

  static void ResetDeadline(Thread& t, SchedDuration rel_deadline,
                            SchedTime start_time) __TA_NO_THREAD_SAFETY_ANALYSIS {
    SchedulerState& ss = t.scheduler_state();

    // Just use 20% for all of our utilizations.  It does not really matter what
    // we pick as our utilization/capacity/timeslice-remaining should not factor
    // into queue ordering right now.
    constexpr SchedUtilization kUtil = SchedUtilization{1} / 5;
    ss.discipline_ = SchedDiscipline::Deadline;
    ss.deadline_ = SchedDeadlineParams{kUtil * rel_deadline, rel_deadline};
    ss.start_time_ = start_time;
    ss.finish_time_ = ss.start_time_ + ss.deadline_.deadline_ns;
  }
};

UNITTEST_START_TESTCASE(wq_order_tests)
UNITTEST("basic", WaitQueueOrderingTests::Test)
UNITTEST_END_TESTCASE(wq_order_tests, "wq_order", "WaitQueue ordering tests")
