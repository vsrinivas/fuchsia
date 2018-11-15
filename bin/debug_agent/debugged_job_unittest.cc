// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/debugged_job.h"

#include <thread>

#include <lib/fdio/spawn.h>
#include <lib/fit/function.h>
#include <zircon/processargs.h>

#include "garnet/bin/debug_agent/object_util.h"
#include "garnet/lib/debug_ipc/helper/message_loop_zircon.h"
#include "gtest/gtest.h"

namespace debug_agent {

namespace {

class JobDebuggerTest : public ::testing::Test, public ProcessStartHandler {
 public:
  void OnProcessStart(zx::process process) override {
    processes_.push_back(std::move(process));
  }

 protected:
  void SetUp() override {
    message_loop_.Init();
    zx::unowned_job current_job(zx_job_default());
    ASSERT_EQ(ZX_OK, zx::job::create(*current_job, 0u, &job_));
  }

  ~JobDebuggerTest() { message_loop_.Cleanup(); }

  static void LaunchProcess(const zx::job& job,
                            const std::vector<const char*>& argv,
                            const char* name, int outfd, zx_handle_t* proc) {
    std::vector<const char*> normalized_argv = argv;
    normalized_argv.push_back(nullptr);

    // Redirect process's stdout to file.
    fdio_spawn_action_t actions[] = {
        {.action = FDIO_SPAWN_ACTION_CLONE_FD,
         .fd = {.local_fd = outfd, .target_fd = STDOUT_FILENO}},
        {.action = FDIO_SPAWN_ACTION_CLONE_FD,
         .fd = {.local_fd = STDIN_FILENO, .target_fd = STDIN_FILENO}},
        {.action = FDIO_SPAWN_ACTION_CLONE_FD,
         .fd = {.local_fd = STDERR_FILENO, .target_fd = STDERR_FILENO}},
        {.action = FDIO_SPAWN_ACTION_SET_NAME, .name = {.data = name}}};
    char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
    zx_status_t status = fdio_spawn_etc(
        job.get(), FDIO_SPAWN_CLONE_ALL, argv[0], normalized_argv.data(),
        nullptr, countof(actions), actions, proc, err_msg);
    ASSERT_EQ(status, ZX_OK) << "Failed to spawn command: " << err_msg;
  }

  // Returns true if condition is true before timeout.
  bool RunLoopWithTimeoutOrUntil(fit::function<bool()> condition,
                                 zx::duration timeout = zx::sec(10),
                                 zx::duration step = zx::msec(10)) {
    const zx::time deadline = (timeout == zx::sec(0))
                                  ? zx::time::infinite()
                                  : zx::deadline_after(timeout);
    while (zx::clock::get_monotonic() < deadline) {
      if (condition()) {
        return true;
      }
      message_loop_.RunUntilTimeout(step);
    }
    return condition();
  }

  void WaitForProcToExit(zx_handle_t proc, int exit_code) {
    zx_info_process_t info;
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] {
      zx_object_get_info(proc, ZX_INFO_PROCESS, &info, sizeof(info), nullptr,
                         nullptr);
      return info.exited;
    }));
    ASSERT_EQ(exit_code, info.return_code);
  }

  std::vector<zx::process> processes_;
  zx::job job_;
  debug_ipc::MessageLoopZircon message_loop_;
};

TEST_F(JobDebuggerTest, OneProcess) {
  zx::job duplicate_job;
  ASSERT_EQ(ZX_OK, job_.duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate_job));
  DebuggedJob debugged_job(this, KoidForObject(duplicate_job),
                           std::move(duplicate_job));
  ASSERT_TRUE(debugged_job.Init());
  debugged_job.SetFilters({"t"});
  ASSERT_EQ(0u, processes_.size());
  zx_handle_t proc = ZX_HANDLE_INVALID;
  int pipefd[2];
  ASSERT_EQ(0, pipe(pipefd));
  const std::vector<const char*> args = {"/system/bin/true"};
  LaunchProcess(job_, args, "true", pipefd[0], &proc);
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([this] {
    return processes_.size() == 1;
  })) << "Expected processes size 1, got: "
      << processes_.size();
  zx::process process = zx::process(proc);
  ASSERT_EQ(KoidForObject(processes_[0]), KoidForObject(process));
  WaitForProcToExit(proc, 0);
}

// Tests that job debug exception is removed when debugged job is killed.
TEST_F(JobDebuggerTest, DebuggedJobKilled) {
  // make sure that job debugger works
  {
    zx::job duplicate_job;
    ASSERT_EQ(ZX_OK, job_.duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate_job));
    DebuggedJob debugged_job(this, KoidForObject(duplicate_job),
                             std::move(duplicate_job));
    ASSERT_TRUE(debugged_job.Init());
    debugged_job.SetFilters({"t"});
    ASSERT_EQ(0u, processes_.size());
    zx_handle_t proc = ZX_HANDLE_INVALID;
    int pipefd[2];
    ASSERT_EQ(0, pipe(pipefd));
    const std::vector<const char*> args = {"/system/bin/true"};
    LaunchProcess(job_, args, "true", pipefd[0], &proc);
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil([this] {
      return processes_.size() == 1;
    })) << "Expected processes size 1, got: "
        << processes_.size();
    zx::process process = zx::process(proc);
    ASSERT_EQ(KoidForObject(processes_[0]), KoidForObject(process));
    WaitForProcToExit(proc, 0);
  }
  // test that new processes are not put into stasis.
  processes_.clear();
  zx_handle_t proc = ZX_HANDLE_INVALID;
  int pipefd[2];
  ASSERT_EQ(0, pipe(pipefd));
  const std::vector<const char*> args = {"/system/bin/true"};
  LaunchProcess(job_, args, "true", pipefd[0], &proc);
  WaitForProcToExit(proc, 0);
  ASSERT_EQ(0u, processes_.size());
}

TEST_F(JobDebuggerTest, MultipleProcesses) {
  zx::job duplicate_job;
  ASSERT_EQ(ZX_OK, job_.duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate_job));
  DebuggedJob debugged_job(this, KoidForObject(duplicate_job),
                           std::move(duplicate_job));
  ASSERT_TRUE(debugged_job.Init());
  debugged_job.SetFilters({"t"});
  ASSERT_EQ(0u, processes_.size());

  int pipefd[2];
  ASSERT_EQ(0, pipe(pipefd));
  const std::vector<const char*> args = {"/system/bin/true"};
  zx_handle_t proc1 = ZX_HANDLE_INVALID;
  zx_handle_t proc2 = ZX_HANDLE_INVALID;

  LaunchProcess(job_, args, "true", pipefd[0], &proc1);
  zx::process process1 = zx::process(proc1);
  auto pid1 = KoidForObject(process1);

  LaunchProcess(job_, args, "true", pipefd[0], &proc2);
  zx::process process2 = zx::process(proc2);
  auto pid2 = KoidForObject(process2);

  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([this] {
    return processes_.size() == 2;
  })) << "Expected processes size 2, got: "
      << processes_.size();
  ASSERT_EQ(KoidForObject(processes_[0]), pid1);
  ASSERT_EQ(KoidForObject(processes_[1]), pid2);
  WaitForProcToExit(proc1, 0);
  WaitForProcToExit(proc2, 0);
}

TEST_F(JobDebuggerTest, ProcessInNestedJob) {
  zx::job child_job;
  ASSERT_EQ(ZX_OK, zx::job::create(job_, 0u, &child_job));
  zx::job duplicate_job;
  ASSERT_EQ(ZX_OK, job_.duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate_job));
  DebuggedJob debugged_job(this, KoidForObject(duplicate_job),
                           std::move(duplicate_job));
  ASSERT_TRUE(debugged_job.Init());
  debugged_job.SetFilters({"t"});
  ASSERT_EQ(0u, processes_.size());
  zx_handle_t proc = ZX_HANDLE_INVALID;
  int pipefd[2];
  ASSERT_EQ(0, pipe(pipefd));
  const std::vector<const char*> args = {"/system/bin/true"};
  LaunchProcess(child_job, args, "true", pipefd[0], &proc);
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([this] {
    return processes_.size() == 1;
  })) << "Expected processes size 1, got: "
      << processes_.size();
  zx::process process = zx::process(proc);
  ASSERT_EQ(KoidForObject(processes_[0]), KoidForObject(process));
  WaitForProcToExit(proc, 0);
}

TEST_F(JobDebuggerTest, FilterFullName) {
  zx::job duplicate_job;
  ASSERT_EQ(ZX_OK, job_.duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate_job));
  DebuggedJob debugged_job(this, KoidForObject(duplicate_job),
                           std::move(duplicate_job));
  ASSERT_TRUE(debugged_job.Init());
  constexpr char name[] = "true";
  debugged_job.SetFilters({name});
  ASSERT_EQ(0u, processes_.size());
  zx_handle_t proc = ZX_HANDLE_INVALID;
  int pipefd[2];
  ASSERT_EQ(0, pipe(pipefd));
  const std::vector<const char*> args = {"/system/bin/true"};
  LaunchProcess(job_, args, name, pipefd[0], &proc);
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([this] {
    return processes_.size() == 1;
  })) << "Expected processes size 1, got: "
      << processes_.size();
  zx::process process = zx::process(proc);
  ASSERT_EQ(KoidForObject(processes_[0]), KoidForObject(process));
  WaitForProcToExit(proc, 0);
}

TEST_F(JobDebuggerTest, FilterMultipleProcess) {
  zx::job duplicate_job;
  ASSERT_EQ(ZX_OK, job_.duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate_job));
  DebuggedJob debugged_job(this, KoidForObject(duplicate_job),
                           std::move(duplicate_job));
  ASSERT_TRUE(debugged_job.Init());
  debugged_job.SetFilters({"t"});
  ASSERT_EQ(0u, processes_.size());

  int pipefd[2];
  ASSERT_EQ(0, pipe(pipefd));
  const std::vector<const char*> args = {"/system/bin/true"};
  zx_handle_t proc1 = ZX_HANDLE_INVALID;
  zx_handle_t proc2 = ZX_HANDLE_INVALID;

  LaunchProcess(job_, args, "false", pipefd[0], &proc1);

  LaunchProcess(job_, args, "true", pipefd[0], &proc2);
  zx::process process = zx::process(proc2);
  auto pid = KoidForObject(process);

  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([this] {
    return processes_.size() == 1;
  })) << "Expected processes size 1, got: "
      << processes_.size();
  ASSERT_EQ(KoidForObject(processes_[0]), pid);
  WaitForProcToExit(proc1, 0);
  WaitForProcToExit(proc2, 0);
}

TEST_F(JobDebuggerTest, MultipleFilters) {
  zx::job duplicate_job;
  ASSERT_EQ(ZX_OK, job_.duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate_job));
  DebuggedJob debugged_job(this, KoidForObject(duplicate_job),
                           std::move(duplicate_job));
  ASSERT_TRUE(debugged_job.Init());
  debugged_job.SetFilters({"t", "f"});
  ASSERT_EQ(0u, processes_.size());

  int pipefd[2];
  ASSERT_EQ(0, pipe(pipefd));
  const std::vector<const char*> args = {"/system/bin/true"};
  zx_handle_t proc1 = ZX_HANDLE_INVALID;
  zx_handle_t proc2 = ZX_HANDLE_INVALID;

  LaunchProcess(job_, args, "false", pipefd[0], &proc1);
  zx::process process1 = zx::process(proc1);
  auto pid1 = KoidForObject(process1);

  LaunchProcess(job_, args, "true", pipefd[0], &proc2);
  zx::process process2 = zx::process(proc2);
  auto pid2 = KoidForObject(process2);

  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([this] {
    return processes_.size() == 2;
  })) << "Expected processes size 2, got: "
      << processes_.size();
  ASSERT_EQ(KoidForObject(processes_[0]), pid1);
  ASSERT_EQ(KoidForObject(processes_[1]), pid2);
  WaitForProcToExit(proc1, 0);
  WaitForProcToExit(proc2, 0);
}

}  // namespace

}  // namespace debug_agent
