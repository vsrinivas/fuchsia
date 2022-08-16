// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZXDUMP_DUMP_TESTS_H_
#define SRC_LIB_ZXDUMP_DUMP_TESTS_H_

#include <lib/fdio/spawn.h>
#include <lib/fitx/result.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zxdump/dump.h>
#include <unistd.h>

#include <cstdio>
#include <vector>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

namespace zxdump::testing {

// A simple test program starts up and waits.
class TestProcess {
 public:
  zx::unowned_process borrow() const { return zx::unowned_process{process_}; }

  ~TestProcess() {
    if (process_) {
      EXPECT_EQ(ZX_OK, process_.kill());
    }
    if (job_ && kill_job_) {
      EXPECT_EQ(ZX_OK, job_.kill());
    }
  }

  void StartChild(std::vector<const char*> args = {}) {
    args.insert(args.begin(), kChildProgram);
    args.push_back(nullptr);
    ASSERT_FALSE(*std::prev(args.end()));
    ASSERT_EQ(ZX_OK, fdio_spawn(job().get(), FDIO_SPAWN_CLONE_ALL, kChildProgram, args.data(),
                                process_.reset_and_get_address()));
  }

  const zx::process& process() const { return process_; }

  zx_koid_t koid() const { return GetKoid(process_); }

  // Explicitly choose the job to use.
  void set_job(zx::job&& job, bool kill_job = false) {
    job_ = std::move(job);
    kill_job_ = kill_job;
  }
  void set_job(const zx::job& job) { ASSERT_EQ(ZX_OK, job.duplicate(ZX_RIGHT_SAME_RIGHTS, &job_)); }

  // This returns the job StartChild will launch the test process in.
  // If set_job hasn't been called, it just uses the default job.
  const zx::job& job() {
    if (!job_) {
      return *default_job_;
    }
    return job_;
  }

  zx_koid_t job_koid() const { return GetKoid(job_); }

  // Create a new empty job and set_job() to that.
  void HermeticJob(const zx::job& parent = *zx::job::default_job()) {
    ASSERT_FALSE(job_);
    ASSERT_EQ(ZX_OK, zx::job::create(parent, 0, &job_));
    kill_job_ = true;
  }

 private:
  static constexpr const char* kChildProgram = "/pkg/bin/zxdump-test-child";

  template <typename Handle>
  static zx_koid_t GetKoid(const Handle& object) {
    zx_info_handle_basic_t info = {.koid = ZX_KOID_INVALID};
    EXPECT_EQ(ZX_OK, object.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
    return info.koid;
  }

  zx::unowned_job default_job_ = zx::job::default_job();
  std::vector<fdio_spawn_action_t> spawn_actions_;
  zx::process process_;
  zx::job job_;
  bool kill_job_ = false;
};

}  // namespace zxdump::testing

#endif  // SRC_LIB_ZXDUMP_DUMP_TESTS_H_
