// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debugger_utils/jobs.h"
#include "garnet/lib/debugger_utils/sysinfo.h"

#include "gtest/gtest.h"

namespace debugserver {
namespace {

zx_koid_t GetKoid(zx_handle_t task) {
  zx_info_handle_basic_t info;
  auto status = zx_object_get_info(task, ZX_INFO_HANDLE_BASIC, &info,
                                   sizeof(info), nullptr, nullptr);
  EXPECT_EQ(status, ZX_OK);
  if (status != ZX_OK) {
    return ZX_KOID_INVALID;
  }

  return info.koid;
}

zx_koid_t GetParentKoid(zx_handle_t task) {
  zx_info_handle_basic_t info;
  auto status = zx_object_get_info(task, ZX_INFO_HANDLE_BASIC, &info,
                                   sizeof(info), nullptr, nullptr);
  EXPECT_EQ(status, ZX_OK);
  if (status != ZX_OK) {
    return ZX_KOID_INVALID;
  }

  return info.related_koid;
}

TEST(JobsTest, ThisJobAndStop) {
  auto job = GetDefaultJob();
  auto jid = GetKoid(job.get());

  JobTreeJobCallback job_callback = [&](zx::job& job, zx_koid_t koid,
                                        zx_koid_t parent_koid,
                                        int depth) -> zx_status_t {
    if (koid == jid) {
      return ZX_ERR_STOP;
    }
    return ZX_OK;
  };
  EXPECT_EQ(WalkJobTree(job, &job_callback, nullptr, nullptr), ZX_ERR_STOP);
  EXPECT_TRUE(job.is_valid());
}

TEST(JobsTest, ThisProcessAndStop) {
  auto job = GetDefaultJob();
  auto pid = GetKoid(zx_process_self());

  JobTreeProcessCallback process_callback =
      [&](zx::process& process, zx_koid_t koid, zx_koid_t parent_koid,
          int depth) -> zx_status_t {
    if (koid == pid) {
      return ZX_ERR_STOP;
    }
    return ZX_OK;
  };
  EXPECT_EQ(WalkJobTree(job, nullptr, &process_callback, nullptr), ZX_ERR_STOP);
}

TEST(JobsTest, ThisThreadAndStop) {
  auto job = GetDefaultJob();
  auto tid = GetKoid(zx_thread_self());

  JobTreeThreadCallback thread_callback =
      [&](zx::thread& thread, zx_koid_t koid, zx_koid_t parent_koid,
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
  return zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, info,
                            sizeof(*info), nullptr, nullptr);
}

static void TestKoids(zx_handle_t task, zx_koid_t koid, zx_koid_t parent_koid) {
  zx_info_handle_basic_t info;
  ASSERT_EQ(GetHandleInfo(task, &info), ZX_OK);
  EXPECT_EQ(koid, info.koid);
  EXPECT_EQ(parent_koid, info.related_koid);
}

static void TestJobProcessThread(zx::job& search_job) {
  auto tid = GetKoid(zx_thread_self());
  auto pid = GetKoid(zx_process_self());
  auto jid = GetParentKoid(zx_process_self());

  int job_depth = -1;
  int process_depth = -1;
  int thread_depth = -1;

  JobTreeJobCallback job_callback = [&](zx::job& task, zx_koid_t koid,
                                        zx_koid_t parent_koid,
                                        int depth) -> zx_status_t {
    TestKoids(task.get(), koid, parent_koid);
    EXPECT_GE(depth, 0);
    if (koid == jid) {
      job_depth = depth;
    }
    return ZX_OK;
  };
  JobTreeProcessCallback process_callback =
      [&](zx::process& task, zx_koid_t koid, zx_koid_t parent_koid,
          int depth) -> zx_status_t {
    TestKoids(task.get(), koid, parent_koid);
    EXPECT_GT(depth, 0);
    if (koid == pid) {
      process_depth = depth;
    }
    return ZX_OK;
  };
  JobTreeThreadCallback thread_callback = [&](zx::thread& task, zx_koid_t koid,
                                              zx_koid_t parent_koid,
                                              int depth) -> zx_status_t {
    TestKoids(task.get(), koid, parent_koid);
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

TEST(JobsTest, JobProcessThread) {
  auto job = GetDefaultJob();
  TestJobProcessThread(job);
}

TEST(JobsTest, RootJob) {
  // Make sure we can find ourselves from the root job.
  // This will likely evolve or be replaced, but it's useful to test
  // current functionality.
  auto job = GetRootJob();
  TestJobProcessThread(job);
}

TEST(JobsTest, FindProcess) {
  auto job = GetDefaultJob();
  auto pid = GetKoid(zx_process_self());
  auto process = FindProcess(job.get(), pid);
  zx_info_handle_basic_t info;
  ASSERT_EQ(GetHandleInfo(process.get(), &info), ZX_OK);
  EXPECT_EQ(info.koid, pid);
}

TEST(JobsTest, TakeRootJobOwnership) {
  auto top_job = GetDefaultJob();
  auto top_job_koid = GetKoid(top_job.get());
  zx::job my_job;

  JobTreeJobCallback job_callback = [&](zx::job& job, zx_koid_t koid,
                                        zx_koid_t parent_koid,
                                        int depth) -> zx_status_t {
    my_job = std::move(job);
    return ZX_OK;
  };
  EXPECT_EQ(WalkJobTree(top_job, &job_callback, nullptr, nullptr), ZX_ERR_STOP);
  EXPECT_FALSE(top_job.is_valid());
  EXPECT_TRUE(my_job.is_valid());
  EXPECT_EQ(GetKoid(my_job.get()), top_job_koid);
}

TEST(JobsTest, TakeChildJobOwnership) {
  auto top_job = GetDefaultJob();
  zx::job child_job;
  ASSERT_EQ(zx::job::create(top_job, 0, &child_job), ZX_OK);
  auto child_job_koid = GetKoid(child_job.get());
  zx::job my_job;

  JobTreeJobCallback job_callback = [&](zx::job& job, zx_koid_t koid,
                                        zx_koid_t parent_koid,
                                        int depth) -> zx_status_t {
    if (koid == child_job_koid) {
      my_job = std::move(job);
    }
    return ZX_OK;
  };
  EXPECT_EQ(WalkJobTree(top_job, &job_callback, nullptr, nullptr), ZX_ERR_STOP);
  EXPECT_TRUE(top_job.is_valid());
  EXPECT_TRUE(my_job.is_valid());
  EXPECT_EQ(GetKoid(my_job.get()), child_job_koid);
}

}  // namespace
}  // namespace debugserver
