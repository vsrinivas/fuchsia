// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <lib/zx/event.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/syscalls/policy.h>

#include <iterator>

#include <fbl/algorithm.h>
#include <mini-process/mini-process.h>
#include <zxtest/zxtest.h>

extern zx_handle_t root_job;

// Job signal that is active when a job has no children (i.e., no child jobs and no child
// processes).
//
// TODO(fxbug.dev/53986): This is a temporary signal that we don't want userspace using (yet?).
// The kernel doesn't export it, but we declare it here to allow it to be tested.
#define ZX_JOB_NO_CHILDREN  __ZX_OBJECT_SIGNAL_6

namespace {

constexpr char kProcessName[] = "job-test-p";

TEST(JobTest, BasicTest) {
  // Never close the launchpad job.
  zx_handle_t job_parent = zx_job_default();
  ASSERT_NE(job_parent, ZX_HANDLE_INVALID);

  // If the parent job is valid, one should be able to create a child job
  // and a child job of the child job.
  zx_handle_t job_child, job_grandchild;
  ASSERT_OK(zx_job_create(job_parent, 0u, &job_child));
  ASSERT_OK(zx_job_create(job_child, 0u, &job_grandchild));

  zx_info_job_t job_info;
  ASSERT_OK(
      zx_object_get_info(job_child, ZX_INFO_JOB, &job_info, sizeof(job_info), nullptr, nullptr));
  EXPECT_FALSE(job_info.exited);
  EXPECT_EQ(job_info.return_code, 0);

  ASSERT_OK(zx_handle_close(job_child));
  ASSERT_OK(zx_handle_close(job_grandchild));

  // If the parent job is not valid it should fail.
  zx_handle_t job_fail;
  ASSERT_STATUS(zx_job_create(ZX_HANDLE_INVALID, 0u, &job_fail), ZX_ERR_BAD_HANDLE);
}

TEST(JobTest, CreateTest) {
  zx_handle_t job_parent = zx_job_default();
  ASSERT_NE(job_parent, ZX_HANDLE_INVALID);

  zx_handle_t job_child;
  ASSERT_OK(zx_job_create(job_parent, 0u, &job_child));

  // Make sure we can create process object with both the parent job and a child job.
  zx_handle_t process1, vmar1;
  ASSERT_OK(
      zx_process_create(job_parent, kProcessName, sizeof(kProcessName), 0u, &process1, &vmar1));

  zx_handle_t process2, vmar2;
  ASSERT_OK(
      zx_process_create(job_child, kProcessName, sizeof(kProcessName), 0u, &process2, &vmar2));

  ASSERT_OK(zx_handle_close(job_child));
  ASSERT_OK(zx_handle_close(process1));
  ASSERT_OK(zx_handle_close(process2));
  ASSERT_OK(zx_handle_close(vmar1));
  ASSERT_OK(zx_handle_close(vmar2));
}

zx_signals_t GetActiveSignals(zx_handle_t object) {
  zx_signals_t observed = 0;
  EXPECT_EQ(zx_object_wait_one(object, 0x0, ZX_TIME_INFINITE_PAST, &observed), ZX_ERR_TIMED_OUT);
  return observed;
}

TEST(JobTest, JobSignals) {
  zx_handle_t job;
  ASSERT_OK(zx_job_create(zx_job_default(), 0u, &job));

  // A new job should have the NO_{JOBS,PROCESSES,CHILDREN} signals set.
  EXPECT_EQ(GetActiveSignals(job), ZX_JOB_NO_PROCESSES | ZX_JOB_NO_JOBS | ZX_JOB_NO_CHILDREN);

  // Create a child process.
  zx_handle_t child_process, vmar;
  ASSERT_OK(zx_process_create(job, kProcessName, sizeof(kProcessName), 0u, &child_process, &vmar));

  // Expect only the NO_JOBS signal now.
  EXPECT_EQ(GetActiveSignals(job), ZX_JOB_NO_JOBS);

  // Create a child job.
  zx_handle_t child_job;
  ASSERT_OK(zx_job_create(job, 0u, &child_job));

  // Expect no signals.
  EXPECT_EQ(GetActiveSignals(job), 0);

  // Kill the process. We expect the NO_PROCESSES signal to activate.
  ASSERT_OK(zx_handle_close(child_process));
  EXPECT_EQ(GetActiveSignals(job), ZX_JOB_NO_PROCESSES);

  // Kill the job. We expect the NO_JOBS and NO_CHILDREN signal to also become active.
  ASSERT_OK(zx_handle_close(child_job));
  EXPECT_EQ(GetActiveSignals(job), ZX_JOB_NO_PROCESSES | ZX_JOB_NO_JOBS | ZX_JOB_NO_CHILDREN);

  ASSERT_OK(zx_handle_close(vmar));
  ASSERT_OK(zx_handle_close(job));
}

TEST(JobTest, CreateMissingRightsTest) {
  zx_rights_t rights = ZX_DEFAULT_JOB_RIGHTS & ~ZX_RIGHT_WRITE & ~ZX_RIGHT_MANAGE_JOB;
  zx_handle_t job_parent;
  zx_status_t status = zx_handle_duplicate(zx_job_default(), rights, &job_parent);
  ASSERT_OK(status);

  zx_handle_t job_child;
  ASSERT_STATUS(zx_job_create(job_parent, 0u, &job_child), ZX_ERR_ACCESS_DENIED);

  zx_handle_close(job_parent);
}

TEST(JobTest, PolicyInvalidTopicTest) {
  zx::job job_child;
  ASSERT_OK(zx::job::create(*zx::job::default_job(), 0u, &job_child));

  const uint32_t invalid_topic = 2u;
  const uint32_t some_policy = 0;
  ASSERT_STATUS(job_child.set_policy(ZX_JOB_POL_RELATIVE, invalid_topic, &some_policy, 1),
                ZX_ERR_INVALID_ARGS);
}

TEST(JobTest, PolicyBasicOverrideDenyTest) {
  zx::job job_child;
  ASSERT_OK(zx::job::create(*zx::job::default_job(), 0u, &job_child));

  zx_policy_basic_v2_t policy[] = {
      {ZX_POL_BAD_HANDLE, ZX_POL_ACTION_KILL, ZX_POL_OVERRIDE_DENY},
      {ZX_POL_NEW_CHANNEL, ZX_POL_ACTION_ALLOW_EXCEPTION, ZX_POL_OVERRIDE_DENY},
      {ZX_POL_NEW_FIFO, ZX_POL_ACTION_DENY, ZX_POL_OVERRIDE_DENY},
  };

  // Set policy that does not allow overrides. Setting the exact same policy succeeds.
  ASSERT_OK(
      job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_BASIC_V2, policy, std::size(policy)));
  ASSERT_OK(
      job_child.set_policy(ZX_JOB_POL_ABSOLUTE, ZX_JOB_POL_BASIC_V2, policy, std::size(policy)));

  // Changing a set policy should fail.
  policy[0].action = ZX_POL_ACTION_ALLOW;
  ASSERT_STATUS(
      job_child.set_policy(ZX_JOB_POL_ABSOLUTE, ZX_JOB_POL_BASIC_V2, policy, std::size(policy)),
      ZX_ERR_ALREADY_EXISTS);
}

TEST(JobTest, PolicyBasicOverrideAllowTest) {
  zx::job job_child;
  ASSERT_OK(zx::job::create(*zx::job::default_job(), 0u, &job_child));

  zx_policy_basic_v2_t policy[] = {
      {ZX_POL_BAD_HANDLE, ZX_POL_ACTION_KILL, ZX_POL_OVERRIDE_ALLOW},
      {ZX_POL_NEW_CHANNEL, ZX_POL_ACTION_ALLOW_EXCEPTION, ZX_POL_OVERRIDE_ALLOW},
      {ZX_POL_NEW_FIFO, ZX_POL_ACTION_DENY, ZX_POL_OVERRIDE_ALLOW},
  };

  // Set policy that does not allow overrides. Setting the exact same policy succeeds.
  ASSERT_OK(
      job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_BASIC_V2, policy, std::size(policy)));

  // Changing a set policy should succeed.
  policy[0].action = ZX_POL_ACTION_ALLOW;
  ASSERT_OK(
      job_child.set_policy(ZX_JOB_POL_ABSOLUTE, ZX_JOB_POL_BASIC_V2, policy, std::size(policy)));
}

TEST(JobTest, PolicyTimerSlackInvalidOptionsTest) {
  zx::job job_child;
  ASSERT_OK(zx::job::create(*zx::job::default_job(), 0u, &job_child));

  zx_policy_timer_slack policy = {ZX_MSEC(10), ZX_TIMER_SLACK_LATE, {}};

  // Invalid.
  uint32_t options = ZX_JOB_POL_ABSOLUTE;
  ASSERT_STATUS(job_child.set_policy(options, ZX_JOB_POL_TIMER_SLACK, &policy, 1),
                ZX_ERR_INVALID_ARGS);

  // Valid.
  options = ZX_JOB_POL_RELATIVE;
  ASSERT_OK(job_child.set_policy(options, ZX_JOB_POL_TIMER_SLACK, &policy, 1));
}

TEST(JobTest, PolicyTimerSlackInvalidCountTest) {
  zx::job job_child;
  ASSERT_OK(zx::job::create(*zx::job::default_job(), 0u, &job_child));

  zx_policy_timer_slack policy[2] = {{ZX_MSEC(10), ZX_TIMER_SLACK_LATE, {}},
                                     {ZX_MSEC(10), ZX_TIMER_SLACK_LATE, {}}};

  // Too few.
  ASSERT_STATUS(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, &policy, 0),
                ZX_ERR_INVALID_ARGS);

  // Too many.
  ASSERT_STATUS(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, &policy, 2),
                ZX_ERR_INVALID_ARGS);

  // Just right.
  ASSERT_OK(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, &policy, 1));
}

TEST(JobTest, PolicyTimerSlackInvalidPolicyTest) {
  zx::job job_child;
  ASSERT_OK(zx::job::create(*zx::job::default_job(), 0u, &job_child));

  // Null.
  ASSERT_STATUS(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, nullptr, 1),
                ZX_ERR_INVALID_ARGS);

  // Negative amount.
  zx_policy_timer_slack policy = {-ZX_MSEC(10), ZX_TIMER_SLACK_LATE, {}};
  ASSERT_STATUS(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, &policy, 1),
                ZX_ERR_INVALID_ARGS);

  // Invalid mode.
  policy = {ZX_MSEC(10), 3, {}};
  ASSERT_STATUS(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, &policy, 1),
                ZX_ERR_INVALID_ARGS);

  // OK.
  policy = {ZX_MSEC(10), ZX_TIMER_SLACK_LATE, {}};
  ASSERT_OK(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, &policy, 1));
}

TEST(JobTest, PolicyTimerSlackNonEmptyTest) {
  zx::job job_child;
  ASSERT_OK(zx::job::create(*zx::job::default_job(), 0u, &job_child));
  zx::job job_grandchild;
  ASSERT_OK(zx::job::create(job_child, 0u, &job_grandchild));

  zx_policy_timer_slack policy = {ZX_MSEC(10), ZX_TIMER_SLACK_LATE, {}};

  // The job isn't empty.
  ASSERT_STATUS(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, &policy, 1),
                ZX_ERR_BAD_STATE);

  job_grandchild.reset();

  // Job is now empty.
  ASSERT_OK(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, &policy, 1));
}

// For verifying timer slack correctness, see |timer_diag()| in kernel/tests/timer_tests.cpp or run
// "k timer_diag".

TEST(JobTest, PolicyTimerSlackValid) {
  zx::job job_child;
  ASSERT_OK(zx::job::create(*zx::job::default_job(), 0u, &job_child));

  // All modes.
  zx_policy_timer_slack policy = {ZX_MSEC(10), ZX_TIMER_SLACK_CENTER, {}};
  ASSERT_OK(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, &policy, 1));
  policy = {ZX_MSEC(10), ZX_TIMER_SLACK_EARLY, {}};
  ASSERT_OK(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, &policy, 1));
  policy = {ZX_MSEC(10), ZX_TIMER_SLACK_LATE, {}};
  ASSERT_OK(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, &policy, 1));

  // Raise the minimum.
  policy = {ZX_SEC(10), ZX_TIMER_SLACK_LATE, {}};
  ASSERT_OK(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, &policy, 1));

  // Try to lower the minimum, no error.
  policy = {ZX_USEC(5), ZX_TIMER_SLACK_CENTER, {}};
  ASSERT_OK(job_child.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_TIMER_SLACK, &policy, 1));
}

TEST(JobTest, KillTest) {
  zx_handle_t job_parent = zx_job_default();
  ASSERT_NE(job_parent, ZX_HANDLE_INVALID);

  zx_handle_t job_child;
  ASSERT_OK(zx_job_create(job_parent, 0u, &job_child));

  zx_handle_t job_grandchild;
  ASSERT_OK(zx_job_create(job_child, 0u, &job_grandchild));

  zx_handle_t event;
  ASSERT_OK(zx_event_create(0u, &event));

  zx_handle_t process, thread;
  ASSERT_OK(start_mini_process(job_child, event, &process, &thread));

  ASSERT_OK(zx_task_kill(job_child));

  zx_signals_t signals;
  ASSERT_OK(zx_object_wait_one(process, ZX_TASK_TERMINATED, ZX_TIME_INFINITE, &signals));
  ASSERT_EQ(signals, ZX_TASK_TERMINATED);

  ASSERT_OK(zx_object_wait_one(job_child, ZX_TASK_TERMINATED, ZX_TIME_INFINITE, &signals));
  ASSERT_EQ(signals,
            ZX_TASK_TERMINATED | ZX_JOB_NO_PROCESSES | ZX_JOB_NO_JOBS | ZX_JOB_NO_CHILDREN);

  // Process should be in the dead state here.
  zx_info_job_t job_info;
  ASSERT_OK(
      zx_object_get_info(job_child, ZX_INFO_JOB, &job_info, sizeof(job_info), nullptr, nullptr));
  EXPECT_TRUE(job_info.exited);
  EXPECT_EQ(job_info.return_code, ZX_TASK_RETCODE_SYSCALL_KILL);

  ASSERT_OK(zx_object_get_info(job_grandchild, ZX_INFO_JOB, &job_info, sizeof(job_info), nullptr,
                               nullptr));
  EXPECT_TRUE(job_info.exited);
  EXPECT_EQ(job_info.return_code, ZX_TASK_RETCODE_SYSCALL_KILL);

  zx_info_process_t proc_info;
  ASSERT_OK(zx_object_get_info(process, ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr,
                               nullptr));
  EXPECT_TRUE(proc_info.exited);
  EXPECT_EQ(proc_info.return_code, ZX_TASK_RETCODE_SYSCALL_KILL);

  // Can't create more processes or jobs.

  zx_handle_t job_grandchild_2;
  ASSERT_STATUS(zx_job_create(job_child, 0u, &job_grandchild_2), ZX_ERR_BAD_STATE);

  ASSERT_OK(zx_handle_close(thread));
  ASSERT_OK(zx_handle_close(process));
  ASSERT_STATUS(start_mini_process(job_child, event, &process, &thread), ZX_ERR_BAD_STATE);

  ASSERT_OK(zx_handle_close(job_child));
  ASSERT_OK(zx_handle_close(job_grandchild));
}

TEST(JobTest, KillJobNoChildTest) {
  zx_handle_t job_parent = zx_job_default();
  ASSERT_NE(job_parent, ZX_HANDLE_INVALID);

  zx_handle_t job_child;
  ASSERT_OK(zx_job_create(job_parent, 0u, &job_child));

  ASSERT_OK(zx_task_kill(job_child));

  zx_handle_t job_grandchild;
  ASSERT_STATUS(zx_job_create(job_child, 0u, &job_grandchild), ZX_ERR_BAD_STATE);

  zx_handle_t event;
  ASSERT_OK(zx_event_create(0u, &event));

  zx_handle_t process, thread;
  ASSERT_STATUS(start_mini_process(job_child, event, &process, &thread), ZX_ERR_BAD_STATE);

  ASSERT_OK(zx_handle_close(job_child));
}

TEST(JobTest, KillJobRemovesFromTree) {
  zx_handle_t job_parent, job_child;
  ASSERT_OK(zx_job_create(zx_job_default(), 0u, &job_parent));
  ASSERT_OK(zx_job_create(job_parent, 0u, &job_child));

  zx_info_handle_basic_t job_child_info;
  ASSERT_OK(zx_object_get_info(job_child, ZX_INFO_HANDLE_BASIC, &job_child_info,
                               sizeof(job_child_info), nullptr, nullptr));

  zx_koid_t children_koids[1] = {ZX_KOID_INVALID};
  size_t num_children = 0;
  ASSERT_OK(zx_object_get_info(job_parent, ZX_INFO_JOB_CHILDREN, children_koids,
                               sizeof(children_koids), nullptr, &num_children));
  ASSERT_EQ(num_children, 1);
  ASSERT_EQ(children_koids[0], job_child_info.koid);

  ASSERT_OK(zx_task_kill(job_child));
  ASSERT_OK(zx_object_wait_one(job_child, ZX_TASK_TERMINATED, ZX_TIME_INFINITE, nullptr));

  ASSERT_OK(zx_object_get_info(job_parent, ZX_INFO_JOB_CHILDREN, children_koids,
                               sizeof(children_koids), nullptr, &num_children));
  ASSERT_EQ(num_children, 0);

  ASSERT_OK(zx_handle_close(job_parent));
  ASSERT_OK(zx_handle_close(job_child));
}

// Jobs aren't always killed, it should also be removed from the tree if the
// last handle is closed when it has no children.
TEST(JobTest, CloseJobRemovesFromTree) {
  zx_handle_t job_parent, job_child;
  ASSERT_OK(zx_job_create(zx_job_default(), 0u, &job_parent));
  ASSERT_OK(zx_job_create(job_parent, 0u, &job_child));

  zx_info_handle_basic_t job_child_info;
  ASSERT_OK(zx_object_get_info(job_child, ZX_INFO_HANDLE_BASIC, &job_child_info,
                               sizeof(job_child_info), nullptr, nullptr));

  zx_koid_t children_koids[1] = {ZX_KOID_INVALID};
  size_t num_children = 0;
  ASSERT_OK(zx_object_get_info(job_parent, ZX_INFO_JOB_CHILDREN, children_koids,
                               sizeof(children_koids), nullptr, &num_children));
  ASSERT_EQ(num_children, 1);
  ASSERT_EQ(children_koids[0], job_child_info.koid);

  ASSERT_OK(zx_handle_close(job_child));

  ASSERT_OK(zx_object_get_info(job_parent, ZX_INFO_JOB_CHILDREN, children_koids,
                               sizeof(children_koids), nullptr, &num_children));
  ASSERT_EQ(num_children, 0);

  ASSERT_OK(zx_handle_close(job_parent));
}

// Make sure a chain of jobs killed from the top cascades properly.
TEST(JobTest, KillJobChain) {
  zx_handle_t jobs[5];
  zx_handle_t parent = zx_job_default();
  for (zx_handle_t& job : jobs) {
    ASSERT_OK(zx_job_create(parent, 0u, &job));
    parent = job;

    zx_handle_t event, process, thread;
    ASSERT_OK(zx_event_create(0u, &event));
    ASSERT_OK(start_mini_process(job, event, &process, &thread));
    ASSERT_OK(zx_handle_close(process));
    ASSERT_OK(zx_handle_close(thread));
  }

  ASSERT_OK(zx_task_kill(jobs[0]));

  // Jobs should terminate bottom-up, so grab the signals right when the top
  // job terminates and all other jobs should have terminated as well.
  zx_wait_item_t wait_items[5] = {{jobs[0], ZX_TASK_TERMINATED, 0},
                                  {jobs[1], 0, 0},
                                  {jobs[2], 0, 0},
                                  {jobs[3], 0, 0},
                                  {jobs[4], 0, 0}};
  ASSERT_OK(zx_object_wait_many(wait_items, countof(wait_items), ZX_TIME_INFINITE));
  for (const zx_wait_item_t& wait_item : wait_items) {
    EXPECT_EQ(wait_item.pending,
              ZX_TASK_TERMINATED | ZX_JOB_NO_PROCESSES | ZX_JOB_NO_JOBS | ZX_JOB_NO_CHILDREN);
  }

  ASSERT_OK(zx_handle_close_many(jobs, countof(jobs)));
}

TEST(JobTest, OneCriticalProcessKillsOneJob) {
  // 1 job, |job|.
  // 1 process, |process|.
  // |process| is a child of |job|.
  zx::job job;
  zx::process process;
  zx::thread thread;
  ASSERT_OK(zx::job::create(*zx::job::default_job(), 0u, &job));
  ASSERT_OK(start_mini_process(job.get(), ZX_HANDLE_INVALID, process.reset_and_get_address(),
                               thread.reset_and_get_address()));
  ASSERT_OK(job.set_critical(0u, process));

  ASSERT_OK(process.kill());

  zx_signals_t observed;
  ASSERT_OK(job.wait_one(ZX_JOB_TERMINATED, zx::time::infinite(), &observed));
  EXPECT_EQ(observed,
            ZX_JOB_TERMINATED | ZX_JOB_NO_PROCESSES | ZX_JOB_NO_JOBS | ZX_JOB_NO_CHILDREN);

  zx_info_job_t job_info;
  ASSERT_OK(job.get_info(ZX_INFO_JOB, &job_info, sizeof(job_info), nullptr, nullptr));
  EXPECT_EQ(job_info.return_code, ZX_TASK_RETCODE_CRITICAL_PROCESS_KILL);
}

TEST(JobTest, ManyCriticalProcessesKillOneJob) {
  // 1 job, |job|.
  // 2 processes, |process1| and |process2|.
  // |process1| and |process2| are children of |job|.
  zx::job job;
  zx::process process1, process2;
  zx::thread thread1, thread2;
  ASSERT_OK(zx::job::create(*zx::job::default_job(), 0u, &job));
  ASSERT_OK(start_mini_process(job.get(), ZX_HANDLE_INVALID, process1.reset_and_get_address(),
                               thread1.reset_and_get_address()));
  ASSERT_OK(start_mini_process(job.get(), ZX_HANDLE_INVALID, process2.reset_and_get_address(),
                               thread2.reset_and_get_address()));
  ASSERT_OK(job.set_critical(0u, process1));
  ASSERT_OK(job.set_critical(0u, process2));

  ASSERT_OK(process1.kill());

  zx_signals_t observed;
  ASSERT_OK(job.wait_one(ZX_JOB_TERMINATED, zx::time::infinite(), &observed));
  EXPECT_EQ(observed,
            ZX_JOB_TERMINATED | ZX_JOB_NO_PROCESSES | ZX_JOB_NO_JOBS | ZX_JOB_NO_CHILDREN);

  zx_info_job_t job_info;
  ASSERT_OK(job.get_info(ZX_INFO_JOB, &job_info, sizeof(job_info), nullptr, nullptr));
  EXPECT_EQ(job_info.return_code, ZX_TASK_RETCODE_CRITICAL_PROCESS_KILL);

  zx_info_process_t process_info;
  ASSERT_OK(
      process2.get_info(ZX_INFO_PROCESS, &process_info, sizeof(process_info), nullptr, nullptr));
  EXPECT_EQ(process_info.return_code, ZX_TASK_RETCODE_CRITICAL_PROCESS_KILL);
}

TEST(JobTest, OneCriticalProcessKillsJobTree) {
  // 2 jobs, |job1| and |job2|.
  // 2 processes, |process1| and |process2|.
  // |job2| is a child of |job1|.
  // |process1| is a child of |job1|, and |process2| is a child of |job2|.
  zx::job job1, job2;
  zx::process process1, process2;
  zx::thread thread1, thread2;
  ASSERT_OK(zx::job::create(*zx::job::default_job(), 0u, &job1));
  ASSERT_OK(zx::job::create(job1, 0u, &job2));
  ASSERT_OK(start_mini_process(job1.get(), ZX_HANDLE_INVALID, process1.reset_and_get_address(),
                               thread1.reset_and_get_address()));
  ASSERT_OK(start_mini_process(job2.get(), ZX_HANDLE_INVALID, process2.reset_and_get_address(),
                               thread2.reset_and_get_address()));
  ASSERT_OK(job1.set_critical(0u, process2));
  ASSERT_STATUS(job2.set_critical(0u, process1), ZX_ERR_INVALID_ARGS);

  ASSERT_OK(process2.kill());

  zx_signals_t observed1, observed2;
  ASSERT_OK(job1.wait_one(ZX_JOB_TERMINATED, zx::time::infinite(), &observed1));
  ASSERT_OK(job2.wait_one(ZX_JOB_TERMINATED, zx::time::infinite(), &observed2));
  EXPECT_EQ(observed1,
            ZX_JOB_TERMINATED | ZX_JOB_NO_PROCESSES | ZX_JOB_NO_JOBS | ZX_JOB_NO_CHILDREN);
  EXPECT_EQ(observed2,
            ZX_JOB_TERMINATED | ZX_JOB_NO_PROCESSES | ZX_JOB_NO_JOBS | ZX_JOB_NO_CHILDREN);

  zx_info_job_t job_info1, job_info2;
  ASSERT_OK(job1.get_info(ZX_INFO_JOB, &job_info1, sizeof(job_info1), nullptr, nullptr));
  ASSERT_OK(job2.get_info(ZX_INFO_JOB, &job_info2, sizeof(job_info2), nullptr, nullptr));
  EXPECT_EQ(job_info1.return_code, ZX_TASK_RETCODE_CRITICAL_PROCESS_KILL);
  EXPECT_EQ(job_info2.return_code, ZX_TASK_RETCODE_CRITICAL_PROCESS_KILL);
}

TEST(JobTest, OneCriticalProcessKillsOneJobIfRetcodeNonzero) {
  // 1 job, |job|.
  // 1 process, |process|.
  // |process| is a child of |job|.
  zx::job job;
  zx::process process;
  zx::thread thread;
  zx::vmar vmar;
  zx::channel channel;
  ASSERT_OK(zx::job::create(*zx::job::default_job(), 0u, &job));
  ASSERT_OK(zx::process::create(job, "", 0u, 0u, &process, &vmar));
  ASSERT_OK(zx::thread::create(process, "", 0u, 0u, &thread));
  ASSERT_OK(start_mini_process_etc(process.get(), thread.get(), vmar.get(), ZX_HANDLE_INVALID, true,
                                   channel.reset_and_get_address()));
  ASSERT_OK(job.set_critical(ZX_JOB_CRITICAL_PROCESS_RETCODE_NONZERO, process));

  ASSERT_OK(mini_process_cmd_send(channel.get(), MINIP_CMD_EXIT_NORMAL));

  zx_signals_t observed;
  ASSERT_OK(job.wait_one(ZX_JOB_NO_PROCESSES, zx::time::infinite(), &observed));
  EXPECT_EQ(observed, ZX_JOB_NO_PROCESSES | ZX_JOB_NO_JOBS | ZX_JOB_NO_CHILDREN);
}

TEST(JobTest, CriticalProcessNotInAncestor) {
  // 1 job, |job|.
  // 1 process, |process|.
  // |process| is not a child of |job|.
  zx::job job;
  zx::process process;
  zx::thread thread;
  ASSERT_OK(zx::job::create(*zx::job::default_job(), 0u, &job));
  ASSERT_OK(start_mini_process(zx_job_default(), ZX_HANDLE_INVALID, process.reset_and_get_address(),
                               thread.reset_and_get_address()));
  ASSERT_STATUS(job.set_critical(0u, process), ZX_ERR_INVALID_ARGS);

  ASSERT_OK(process.kill());
}

TEST(JobTest, CriticalProcessAlreadySet) {
  // 1 job, |job|.
  // 1 process, |process|.
  // |process| is a child of |job|.
  zx::job job;
  zx::process process;
  zx::thread thread;
  ASSERT_OK(zx::job::create(*zx::job::default_job(), 0u, &job));
  ASSERT_OK(start_mini_process(job.get(), ZX_HANDLE_INVALID, process.reset_and_get_address(),
                               thread.reset_and_get_address()));
  ASSERT_OK(job.set_critical(0u, process));
  ASSERT_STATUS(job.set_critical(0u, process), ZX_ERR_ALREADY_BOUND);

  ASSERT_OK(process.kill());

  zx_signals_t observed;
  ASSERT_OK(job.wait_one(ZX_JOB_TERMINATED, zx::time::infinite(), &observed));
  EXPECT_EQ(observed,
            ZX_JOB_TERMINATED | ZX_JOB_NO_PROCESSES | ZX_JOB_NO_JOBS | ZX_JOB_NO_CHILDREN);

  zx_info_job_t job_info;
  ASSERT_OK(job.get_info(ZX_INFO_JOB, &job_info, sizeof(job_info), nullptr, nullptr));
  EXPECT_EQ(job_info.return_code, ZX_TASK_RETCODE_CRITICAL_PROCESS_KILL);
}

TEST(JobTest, SetJobOomKillBit) {
  // TODO(cpu): Other than trivial set/reset of the property this can't be
  // fully tested without de-establizing the system under test. The current
  // best way to test this is to boot the full stack and issue in a console
  //   $k oom lowmem
  // And watch the kernel log output.

  size_t oom = 1u;
  ASSERT_OK(
      zx_object_set_property(zx_job_default(), ZX_PROP_JOB_KILL_ON_OOM, &oom, sizeof(size_t)));

  oom = 0u;
  ASSERT_OK(
      zx_object_set_property(zx_job_default(), ZX_PROP_JOB_KILL_ON_OOM, &oom, sizeof(size_t)));

  oom = 2u;
  ASSERT_STATUS(
      zx_object_set_property(zx_job_default(), ZX_PROP_JOB_KILL_ON_OOM, &oom, sizeof(size_t)),
      ZX_ERR_INVALID_ARGS);
}

TEST(JobTest, WaitTest) {
  zx_handle_t job_parent = zx_job_default();
  ASSERT_NE(job_parent, ZX_HANDLE_INVALID);

  zx_handle_t job_child;
  ASSERT_OK(zx_job_create(job_parent, 0u, &job_child));

  zx_handle_t event;
  ASSERT_OK(zx_event_create(0u, &event));

  zx_handle_t process, thread;
  ASSERT_OK(start_mini_process(job_child, event, &process, &thread));

  zx_signals_t signals;
  ASSERT_OK(zx_object_wait_one(job_child, ZX_JOB_NO_JOBS, ZX_TIME_INFINITE, &signals));
  ASSERT_EQ(signals, ZX_JOB_NO_JOBS);

  zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));
  ASSERT_OK(zx_task_kill(process));

  ASSERT_OK(zx_object_wait_one(job_child, ZX_JOB_NO_PROCESSES, ZX_TIME_INFINITE, &signals));
  ASSERT_EQ(signals, ZX_JOB_NO_PROCESSES | ZX_JOB_NO_JOBS | ZX_JOB_NO_CHILDREN);

  ASSERT_OK(zx_handle_close(thread));
  ASSERT_OK(zx_handle_close(process));
  ASSERT_OK(zx_handle_close(job_child));
}

TEST(JobTest, InfoTaskStatsFails) {
  zx_info_task_stats_t info;
  ASSERT_NOT_OK(zx_object_get_info(zx_job_default(), ZX_INFO_TASK_STATS, &info, sizeof(info),
                                   nullptr, nullptr),
                "Just added job support to info_task_status?");
  // If so, replace this with a real test; see example in process.cpp.
}

// Show that there is a max job height.
TEST(JobTest, MaxHeightSmoke) {
  // Get our parent job.
  zx_handle_t parent_job = zx_job_default();

  // Stack of handles that we need to close.
  static constexpr int kNumJobs = 128;
  zx_handle_t* handles = static_cast<zx_handle_t*>(calloc(kNumJobs, sizeof(*handles)));
  ASSERT_TRUE(handles);
  zx_handle_t* htop = handles;

  // Eat up our max height.
  while (true) {
    zx_handle_t child_job;
    zx_status_t s = zx_job_create(parent_job, 0u, &child_job);
    if (s != ZX_OK) {
      break;
    }
    // We should hit the max before running out of entries;
    // this is the core check of this test.
    ASSERT_LT(htop - handles, kNumJobs, "Should have seen the max job height");
    *htop++ = child_job;
    parent_job = child_job;
  }

  // We've hit the bottom. Creating a child under this job should fail.
  zx_handle_t child_job;
  EXPECT_STATUS(zx_job_create(parent_job, 0u, &child_job), ZX_ERR_OUT_OF_RANGE);

  // Creating a process should succeed, though.
  zx_handle_t child_proc;
  zx_handle_t vmar;
  ASSERT_OK(zx_process_create(parent_job, "test", sizeof("test"), 0u, &child_proc, &vmar));
  zx_handle_close(vmar);
  zx_handle_close(child_proc);

  // Clean up.
  while (htop > handles) {
    EXPECT_OK(zx_handle_close(*--htop));
  }
  free(handles);
}

TEST(JobTest, GetRuntimeTest) {
  zx::job job_child;
  ASSERT_OK(zx::job::create(*zx::job::default_job(), 0u, &job_child));

  zx_info_task_runtime_t info;
  ASSERT_OK(job_child.get_info(ZX_INFO_TASK_RUNTIME, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(info.cpu_time, 0);
  EXPECT_EQ(info.queue_time, 0);

  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));

  zx::process process;
  zx::thread thread;
  ASSERT_OK(start_mini_process(job_child.get(), event.get(), process.reset_and_get_address(),
                               thread.reset_and_get_address()));

  ASSERT_OK(thread.wait_one(ZX_THREAD_RUNNING, zx::time::infinite(), nullptr));

  ASSERT_OK(job_child.get_info(ZX_INFO_TASK_RUNTIME, &info, sizeof(info), nullptr, nullptr));
  EXPECT_GT(info.cpu_time, 0);
  EXPECT_GT(info.queue_time, 0);

  // Check we can still read the task runtimes after the job is terminates, and that they don't
  // change.
  ASSERT_OK(job_child.kill());
  ASSERT_OK(job_child.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr));

  ASSERT_OK(job_child.get_info(ZX_INFO_TASK_RUNTIME, &info, sizeof(info), nullptr, nullptr));
  EXPECT_GT(info.cpu_time, 0);
  EXPECT_GT(info.queue_time, 0);

  zx_info_task_runtime_t info2;
  ASSERT_OK(job_child.get_info(ZX_INFO_TASK_RUNTIME, &info2, sizeof(info2), nullptr, nullptr));
  EXPECT_EQ(info.cpu_time, info2.cpu_time);
  EXPECT_EQ(info.queue_time, info2.queue_time);

  // Check that we cannot get info anymore if we remove ZX_RIGHT_INSPECT
  zx_info_handle_basic_t basic;
  ASSERT_OK(job_child.get_info(ZX_INFO_HANDLE_BASIC, &basic, sizeof(basic), nullptr, nullptr));
  zx::job job_child_dup;
  ASSERT_OK(job_child.duplicate(basic.rights & ~ZX_RIGHT_INSPECT, &job_child_dup));
  EXPECT_EQ(job_child_dup.get_info(ZX_INFO_TASK_RUNTIME, &info, sizeof(info), nullptr, nullptr),
            ZX_ERR_ACCESS_DENIED);
}

}  // namespace
