// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/scheduler.h>
#include <unittest/unittest.h>

#include "unittest_utils.h"

namespace {

class fake_context : public fit::context {
 public:
  fit::executor* executor() const override { ASSERT_CRITICAL(false); }
  fit::suspended_task suspend_task() override { ASSERT_CRITICAL(false); }
};

fit::pending_task make_pending_task(uint64_t* counter) {
  return fit::make_promise([counter] { (*counter)++; });
}

bool initial_state() {
  BEGIN_TEST;

  fit::subtle::scheduler scheduler;
  EXPECT_FALSE(scheduler.has_runnable_tasks());
  EXPECT_FALSE(scheduler.has_suspended_tasks());
  EXPECT_FALSE(scheduler.has_outstanding_tickets());

  END_TEST;
}

bool schedule_task() {
  BEGIN_TEST;

  fit::subtle::scheduler scheduler;
  fit::subtle::scheduler::task_queue tasks;
  fake_context context;
  uint64_t run_count[3] = {};

  // Initially there are no tasks.
  scheduler.take_runnable_tasks(&tasks);
  EXPECT_TRUE(tasks.empty());

  // Schedule and run one task.
  scheduler.schedule_task(make_pending_task(&run_count[0]));
  EXPECT_TRUE(scheduler.has_runnable_tasks());
  EXPECT_FALSE(scheduler.has_suspended_tasks());
  EXPECT_FALSE(scheduler.has_outstanding_tickets());
  scheduler.take_runnable_tasks(&tasks);
  EXPECT_EQ(1, tasks.size());
  tasks.front()(context);
  EXPECT_EQ(1, run_count[0]);
  tasks.pop();

  // Run a couple more, ensure that they come out in queue order.
  scheduler.schedule_task(make_pending_task(&run_count[0]));
  scheduler.schedule_task(make_pending_task(&run_count[1]));
  scheduler.schedule_task(make_pending_task(&run_count[2]));
  EXPECT_TRUE(scheduler.has_runnable_tasks());
  EXPECT_FALSE(scheduler.has_suspended_tasks());
  EXPECT_FALSE(scheduler.has_outstanding_tickets());
  scheduler.take_runnable_tasks(&tasks);
  EXPECT_EQ(3, tasks.size());
  tasks.front()(context);
  EXPECT_EQ(2, run_count[0]);
  EXPECT_EQ(0, run_count[1]);
  EXPECT_EQ(0, run_count[2]);
  tasks.pop();
  tasks.front()(context);
  EXPECT_EQ(2, run_count[0]);
  EXPECT_EQ(1, run_count[1]);
  EXPECT_EQ(0, run_count[2]);
  tasks.pop();
  tasks.front()(context);
  EXPECT_EQ(2, run_count[0]);
  EXPECT_EQ(1, run_count[1]);
  EXPECT_EQ(1, run_count[2]);
  tasks.pop();

  // Once we're done, no tasks are left.
  scheduler.take_runnable_tasks(&tasks);
  EXPECT_TRUE(tasks.empty());

  END_TEST;
}

bool ticket_obtain_finalize_without_task() {
  BEGIN_TEST;

  fit::subtle::scheduler scheduler;

  fit::suspended_task::ticket t = scheduler.obtain_ticket();
  EXPECT_FALSE(scheduler.has_runnable_tasks());
  EXPECT_FALSE(scheduler.has_suspended_tasks());
  EXPECT_TRUE(scheduler.has_outstanding_tickets());

  fit::pending_task task;
  scheduler.finalize_ticket(t, &task);
  EXPECT_FALSE(scheduler.has_runnable_tasks());
  EXPECT_FALSE(scheduler.has_suspended_tasks());
  EXPECT_FALSE(scheduler.has_outstanding_tickets());

  END_TEST;
}

bool ticket_obtain_finalize_with_task() {
  BEGIN_TEST;

  fit::subtle::scheduler scheduler;

  fit::suspended_task::ticket t = scheduler.obtain_ticket();
  EXPECT_FALSE(scheduler.has_runnable_tasks());
  EXPECT_FALSE(scheduler.has_suspended_tasks());
  EXPECT_TRUE(scheduler.has_outstanding_tickets());

  uint64_t run_count = 0;
  fit::pending_task p = make_pending_task(&run_count);
  scheduler.finalize_ticket(t, &p);
  EXPECT_FALSE(scheduler.has_runnable_tasks());
  EXPECT_FALSE(scheduler.has_suspended_tasks());
  EXPECT_FALSE(scheduler.has_outstanding_tickets());
  EXPECT_TRUE(p);  // didn't take ownership

  END_TEST;
}

bool ticket_obtain2_duplicate_finalize_release() {
  BEGIN_TEST;

  fit::subtle::scheduler scheduler;

  fit::suspended_task::ticket t = scheduler.obtain_ticket(2 /*initial_refs*/);
  scheduler.duplicate_ticket(t);
  EXPECT_FALSE(scheduler.has_runnable_tasks());
  EXPECT_FALSE(scheduler.has_suspended_tasks());
  EXPECT_TRUE(scheduler.has_outstanding_tickets());

  uint64_t run_count = 0;
  fit::pending_task p = make_pending_task(&run_count);
  scheduler.finalize_ticket(t, &p);
  EXPECT_FALSE(scheduler.has_runnable_tasks());
  EXPECT_TRUE(scheduler.has_suspended_tasks());
  EXPECT_TRUE(scheduler.has_outstanding_tickets());
  EXPECT_FALSE(p);  // took ownership

  p = scheduler.release_ticket(t);
  EXPECT_FALSE(scheduler.has_runnable_tasks());
  EXPECT_TRUE(scheduler.has_suspended_tasks());
  EXPECT_TRUE(scheduler.has_outstanding_tickets());
  EXPECT_FALSE(p);  // ticket still has one ref

  p = scheduler.release_ticket(t);
  EXPECT_FALSE(scheduler.has_runnable_tasks());
  EXPECT_FALSE(scheduler.has_suspended_tasks());
  EXPECT_FALSE(scheduler.has_outstanding_tickets());
  EXPECT_TRUE(p);  // ticket fully unref'd so task ownership returned

  END_TEST;
}

bool ticket_obtain2_duplicate_finalize_resume() {
  BEGIN_TEST;

  fit::subtle::scheduler scheduler;

  fit::suspended_task::ticket t = scheduler.obtain_ticket(2 /*initial_refs*/);
  scheduler.duplicate_ticket(t);
  EXPECT_FALSE(scheduler.has_runnable_tasks());
  EXPECT_FALSE(scheduler.has_suspended_tasks());
  EXPECT_TRUE(scheduler.has_outstanding_tickets());

  uint64_t run_count = 0;
  fit::pending_task p = make_pending_task(&run_count);
  scheduler.finalize_ticket(t, &p);
  EXPECT_FALSE(scheduler.has_runnable_tasks());
  EXPECT_TRUE(scheduler.has_suspended_tasks());
  EXPECT_TRUE(scheduler.has_outstanding_tickets());
  EXPECT_FALSE(p);  // took ownership

  scheduler.resume_task_with_ticket(t);
  EXPECT_TRUE(scheduler.has_runnable_tasks());
  EXPECT_FALSE(scheduler.has_suspended_tasks());
  EXPECT_TRUE(scheduler.has_outstanding_tickets());

  p = scheduler.release_ticket(t);
  EXPECT_TRUE(scheduler.has_runnable_tasks());
  EXPECT_FALSE(scheduler.has_suspended_tasks());
  EXPECT_FALSE(scheduler.has_outstanding_tickets());
  EXPECT_FALSE(p);  // ticket was already resumed, nothing to return

  fit::subtle::scheduler::task_queue tasks;
  scheduler.take_runnable_tasks(&tasks);
  EXPECT_EQ(1, tasks.size());

  fake_context context;
  tasks.front()(context);
  EXPECT_EQ(1, run_count);

  END_TEST;
}

bool ticket_obtain2_release_finalize() {
  BEGIN_TEST;

  fit::subtle::scheduler scheduler;

  fit::suspended_task::ticket t = scheduler.obtain_ticket(2 /*initial_refs*/);
  EXPECT_FALSE(scheduler.has_runnable_tasks());
  EXPECT_FALSE(scheduler.has_suspended_tasks());
  EXPECT_TRUE(scheduler.has_outstanding_tickets());

  fit::pending_task p = scheduler.release_ticket(t);
  EXPECT_FALSE(scheduler.has_runnable_tasks());
  EXPECT_FALSE(scheduler.has_suspended_tasks());
  EXPECT_TRUE(scheduler.has_outstanding_tickets());
  EXPECT_FALSE(p);  // ticket still has one ref

  uint64_t run_count = 0;
  p = make_pending_task(&run_count);
  scheduler.finalize_ticket(t, &p);
  EXPECT_FALSE(scheduler.has_runnable_tasks());
  EXPECT_FALSE(scheduler.has_suspended_tasks());
  EXPECT_FALSE(scheduler.has_outstanding_tickets());
  EXPECT_TRUE(p);  // ticket ref-count reached zero, so ownership not taken

  END_TEST;
}

bool ticket_obtain2_resume_finalize() {
  BEGIN_TEST;

  fit::subtle::scheduler scheduler;

  fit::suspended_task::ticket t = scheduler.obtain_ticket(2 /*initial_refs*/);
  EXPECT_FALSE(scheduler.has_runnable_tasks());
  EXPECT_FALSE(scheduler.has_suspended_tasks());
  EXPECT_TRUE(scheduler.has_outstanding_tickets());

  scheduler.resume_task_with_ticket(t);
  EXPECT_FALSE(scheduler.has_runnable_tasks());
  EXPECT_FALSE(scheduler.has_suspended_tasks());
  EXPECT_TRUE(scheduler.has_outstanding_tickets());

  uint64_t run_count = 0;
  fit::pending_task p = make_pending_task(&run_count);
  scheduler.finalize_ticket(t, &p);
  EXPECT_TRUE(scheduler.has_runnable_tasks());
  EXPECT_FALSE(scheduler.has_suspended_tasks());
  EXPECT_FALSE(scheduler.has_outstanding_tickets());
  EXPECT_FALSE(p);  // took ownership since task already resumed

  fit::subtle::scheduler::task_queue tasks;
  scheduler.take_runnable_tasks(&tasks);
  EXPECT_EQ(1, tasks.size());

  fake_context context;
  tasks.front()(context);
  EXPECT_EQ(1, run_count);

  END_TEST;
}

bool take_all_tasks() {
  BEGIN_TEST;

  fit::subtle::scheduler scheduler;
  fit::subtle::scheduler::task_queue tasks;
  fake_context context;
  uint64_t run_count[6] = {};

  // Initially there are no tasks.
  scheduler.take_all_tasks(&tasks);
  EXPECT_TRUE(tasks.empty());

  // Schedule a task.
  scheduler.schedule_task(make_pending_task(&run_count[0]));
  EXPECT_TRUE(scheduler.has_runnable_tasks());

  // Suspend a task and finalize it without resumption.
  // This does not leave an outstanding ticket.
  fit::suspended_task::ticket t1 = scheduler.obtain_ticket();
  fit::pending_task p1 = make_pending_task(&run_count[1]);
  scheduler.finalize_ticket(t1, &p1);
  EXPECT_TRUE(p1);  // took ownership

  // Suspend a task and duplicate its ticket.
  // This leaves an outstanding ticket with an associated task.
  fit::suspended_task::ticket t2 = scheduler.obtain_ticket();
  fit::pending_task p2 = make_pending_task(&run_count[2]);
  scheduler.duplicate_ticket(t2);
  scheduler.finalize_ticket(t2, &p2);
  EXPECT_FALSE(p2);  // didn't take ownership

  // Suspend a task, duplicate its ticket, then release it.
  // This does not leave an outstanding ticket.
  fit::suspended_task::ticket t3 = scheduler.obtain_ticket();
  fit::pending_task p3 = make_pending_task(&run_count[3]);
  scheduler.duplicate_ticket(t3);
  scheduler.finalize_ticket(t3, &p3);
  EXPECT_FALSE(p3);  // didn't take ownership
  p3 = scheduler.release_ticket(t3);
  EXPECT_TRUE(p3);

  // Suspend a task, duplicate its ticket, then resume it.
  // This adds a runnable task but does not leave an outstanding ticket.
  fit::suspended_task::ticket t4 = scheduler.obtain_ticket();
  fit::pending_task p4 = make_pending_task(&run_count[4]);
  scheduler.duplicate_ticket(t4);
  scheduler.finalize_ticket(t4, &p4);
  EXPECT_FALSE(p4);  // didn't take ownership
  EXPECT_TRUE(scheduler.resume_task_with_ticket(t4));

  // Suspend a task, duplicate its ticket twice, then resume it.
  // This adds a runnable task and leaves an outstanding ticket without an
  // associated task.
  fit::suspended_task::ticket t5 = scheduler.obtain_ticket();
  fit::pending_task p5 = make_pending_task(&run_count[5]);
  scheduler.duplicate_ticket(t5);
  scheduler.duplicate_ticket(t5);
  scheduler.finalize_ticket(t5, &p5);
  EXPECT_FALSE(p5);  // didn't take ownership
  EXPECT_TRUE(scheduler.resume_task_with_ticket(t5));

  // Now take all tasks.
  // We expect to find tasks that were runnable or associated with
  // outstanding tickets.  Those outstanding tickets will remain, however they
  // no longer have an associated task (cannot subsequently be resumed).
  EXPECT_TRUE(scheduler.has_runnable_tasks());
  EXPECT_TRUE(scheduler.has_suspended_tasks());
  EXPECT_TRUE(scheduler.has_outstanding_tickets());
  scheduler.take_all_tasks(&tasks);
  EXPECT_FALSE(scheduler.has_runnable_tasks());
  EXPECT_FALSE(scheduler.has_suspended_tasks());
  EXPECT_TRUE(scheduler.has_outstanding_tickets());

  // Check that we obtained the tasks we expected to obtain, by running them.
  EXPECT_EQ(4, tasks.size());
  while (!tasks.empty()) {
    tasks.front()(context);
    tasks.pop();
  }
  EXPECT_EQ(1, run_count[0]);
  EXPECT_EQ(0, run_count[1]);
  EXPECT_EQ(1, run_count[2]);
  EXPECT_EQ(0, run_count[3]);
  EXPECT_EQ(1, run_count[4]);
  EXPECT_EQ(1, run_count[5]);

  // Now that everything is gone, taking all tasks should return an empty set.
  scheduler.take_all_tasks(&tasks);
  EXPECT_FALSE(scheduler.has_runnable_tasks());
  EXPECT_FALSE(scheduler.has_suspended_tasks());
  EXPECT_TRUE(scheduler.has_outstanding_tickets());
  EXPECT_TRUE(tasks.empty());

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(scheduler_tests)
RUN_TEST(initial_state)
RUN_TEST(schedule_task)
RUN_TEST(ticket_obtain_finalize_without_task)
RUN_TEST(ticket_obtain_finalize_with_task)
RUN_TEST(ticket_obtain2_duplicate_finalize_release)
RUN_TEST(ticket_obtain2_duplicate_finalize_resume)
RUN_TEST(ticket_obtain2_release_finalize)
RUN_TEST(ticket_obtain2_resume_finalize)
RUN_TEST(take_all_tasks)
END_TEST_CASE(scheduler_tests)
