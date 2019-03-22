// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "garnet/lib/debugger_utils/jobs.h"
#include "garnet/lib/debugger_utils/sysinfo.h"
#include "garnet/lib/debugger_utils/test_helper.h"
#include "garnet/lib/debugger_utils/test_helper_fixture.h"
#include "garnet/lib/debugger_utils/util.h"

namespace debugger_utils {
namespace {

TEST(JobsTest, SkipTopJob) {
  auto job = GetDefaultJob();
  auto jid = GetKoid(job.get());

  JobTreeJobCallback job_callback = [&](zx::job* job, zx_koid_t koid,
                                        zx_koid_t parent_koid,
                                        int depth) -> zx_status_t {
    if (koid == jid) {
      return ZX_ERR_STOP;
    }
    return ZX_OK;
  };
  EXPECT_EQ(WalkJobTree(job, &job_callback, nullptr, nullptr), ZX_OK);
  EXPECT_TRUE(job.is_valid());
}

TEST(JobsTest, ThisProcessAndStop) {
  auto job = GetDefaultJob();
  auto pid = GetKoid(zx_process_self());

  JobTreeProcessCallback process_callback =
      [&](zx::process* process, zx_koid_t koid, zx_koid_t parent_koid,
          int depth) -> zx_status_t {
    if (koid == pid) {
      return ZX_ERR_STOP;
    }
    return ZX_OK;
  };
  EXPECT_EQ(WalkJobTree(job, nullptr, &process_callback, nullptr),
            ZX_ERR_STOP);
}

TEST(JobsTest, ThisThreadAndStop) {
  auto job = GetDefaultJob();
  auto tid = GetKoid(zx_thread_self());

  JobTreeThreadCallback thread_callback =
      [&](zx::thread* thread, zx_koid_t koid, zx_koid_t parent_koid,
          int depth) -> zx_status_t {
    if (koid == tid) {
      return ZX_ERR_STOP;
    }
    return ZX_OK;
  };
  EXPECT_EQ(WalkJobTree(job, nullptr, nullptr, &thread_callback), ZX_ERR_STOP);
}

static zx_status_t GetHandleInfo(zx_handle_t handle,
                                 zx_info_handle_basic_t* info) {
  return zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, info, sizeof(*info),
                            nullptr, nullptr);
}

static void TestKoids(zx_handle_t task, zx_koid_t koid, zx_koid_t parent_koid) {
  zx_info_handle_basic_t info;
  ASSERT_EQ(GetHandleInfo(task, &info), ZX_OK);
  EXPECT_EQ(koid, info.koid);
  EXPECT_EQ(parent_koid, info.related_koid);
}

static void TestDepth(const zx::job& search_job, zx_handle_t target_job,
                      zx_handle_t target_process, zx_handle_t target_thread) {
  auto jid = GetKoid(target_job);
  auto pid = GetKoid(target_process);
  auto tid = GetKoid(target_thread);

  // |WalkJobTree()| doesn't search the job it's passed.
  int job_depth;
  if (GetKoid(search_job.get()) == jid) {
    job_depth = 0;
  } else {
    job_depth = -1;
  }
  int process_depth = -1;
  int thread_depth = -1;

  JobTreeJobCallback job_callback =
    [&](zx::job* task, zx_koid_t koid, zx_koid_t parent_koid,
        int depth) -> zx_status_t {
    TestKoids(task->get(), koid, parent_koid);
    EXPECT_GE(depth, 0);
    if (koid == jid) {
      job_depth = depth;
    }
    return ZX_OK;
  };
  JobTreeProcessCallback process_callback =
      [&](zx::process* task, zx_koid_t koid, zx_koid_t parent_koid,
          int depth) -> zx_status_t {
    TestKoids(task->get(), koid, parent_koid);
    EXPECT_GT(depth, 0);
    if (koid == pid) {
      process_depth = depth;
    }
    return ZX_OK;
  };
  JobTreeThreadCallback thread_callback =
    [&](zx::thread* task, zx_koid_t koid, zx_koid_t parent_koid,
        int depth) -> zx_status_t {
    TestKoids(task->get(), koid, parent_koid);
    EXPECT_GT(depth, 1);
    if (koid == tid) {
      thread_depth = depth;
    }
    return ZX_OK;
  };

  EXPECT_EQ(WalkJobTree(search_job, &job_callback, &process_callback,
                        &thread_callback),
            ZX_OK);
  EXPECT_EQ(job_depth + 1, process_depth);
  EXPECT_EQ(process_depth + 1, thread_depth);
}

TEST_F(TestWithHelper, JobsTestChildJob) {
  auto parent_job = GetDefaultJob();
  zx::job child_job;
  zx::thread thread;

  ASSERT_EQ(zx::job::create(parent_job, 0, &child_job), ZX_OK);
  ASSERT_EQ(RunHelperProgram(child_job, kWaitPeerClosedArgv), ZX_OK);

  // We need the handle of the main thread of the process for test purposes.
  // Technically, we only need its koid. HOWEVER, we do not obtain the koid
  // via, say, ZX_INFO_PROCESS_THREADS, because that is used by the routine
  // we are testing: |WalkJobTree()|. Try to KISS and just get the thread's
  // handle. Alas it's not that simple.
  ASSERT_EQ(GetHelperThread(&thread), ZX_OK);

  ASSERT_TRUE(child_job.is_valid());
  ASSERT_TRUE(process().is_valid());
  ASSERT_TRUE(thread.is_valid());
  ASSERT_TRUE(channel().is_valid());

  TestDepth(parent_job, child_job.get(), process().get(), thread.get());
}

TEST_F(TestWithHelper, JobsTestRootJob) {
  // Make sure we can find ourselves from the root job.
  // This will likely evolve or be replaced, but it's useful to test
  // current functionality.
  auto search_job = GetRootJob();
  auto parent_job = GetDefaultJob();
  zx::job child_job;
  zx::thread thread;

  ASSERT_EQ(zx::job::create(parent_job, 0, &child_job), ZX_OK);
  ASSERT_EQ(RunHelperProgram(child_job, kWaitPeerClosedArgv), ZX_OK);
  ASSERT_EQ(GetHelperThread(&thread), ZX_OK);

  ASSERT_TRUE(child_job.is_valid());
  ASSERT_TRUE(process().is_valid());
  ASSERT_TRUE(thread.is_valid());
  ASSERT_TRUE(channel().is_valid());

  TestDepth(search_job, child_job.get(), process().get(), thread.get());
}

TEST(JobsTest, FindProcess) {
  auto job = GetDefaultJob();
  auto pid = GetKoid(zx_process_self());
  auto process = FindProcess(job.get(), pid);
  zx_info_handle_basic_t info;
  ASSERT_EQ(GetHandleInfo(process.get(), &info), ZX_OK);
  EXPECT_EQ(info.koid, pid);
}

TEST(JobsTest, TakeChildJobOwnership) {
  auto top_job = GetDefaultJob();
  zx::job parent_job;
  ASSERT_EQ(zx::job::create(top_job, 0, &parent_job), ZX_OK);
  zx::job child_job;
  ASSERT_EQ(zx::job::create(parent_job, 0, &child_job), ZX_OK);
  auto child_job_koid = GetKoid(child_job.get());
  zx::job my_job;

  JobTreeJobCallback job_callback = [&](zx::job* job, zx_koid_t koid,
                                        zx_koid_t parent_koid,
                                        int depth) -> zx_status_t {
    if (koid == child_job_koid) {
      EXPECT_EQ(depth, 2);
      my_job = std::move(*job);
      return ZX_ERR_STOP;
    }
    return ZX_OK;
  };
  EXPECT_EQ(WalkJobTree(top_job, &job_callback, nullptr, nullptr),
            ZX_ERR_STOP);
  EXPECT_TRUE(top_job.is_valid());
  EXPECT_TRUE(my_job.is_valid());
  EXPECT_EQ(GetKoid(my_job.get()), child_job_koid);
}

}  // namespace
}  // namespace debugger_utils
