// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/function.h>
#include <lib/zx/clock.h>
#include <lib/zx/event.h>
#include <lib/zx/job.h>
#include <lib/zx/msi.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <lib/zx/vmar.h>
#include <threads.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <atomic>
#include <cassert>
#include <climits>

#include <mini-process/mini-process.h>
#include <zxtest/zxtest.h>

namespace {

#ifdef __aarch64__
constexpr auto kThreadRegister = &zx_thread_state_general_regs_t::tpidr;
#elif defined(__x86_64__)
constexpr auto kThreadRegister = &zx_thread_state_general_regs_t::fs_base;
#endif

const zx_time_t kTimeoutNs = ZX_MSEC(250);

TEST(ProcessTest, LongNameSucceeds) {
  // Creating a process with a super long name should succeed.
  static const char long_name[] =
      "0123456789012345678901234567890123456789"
      "0123456789012345678901234567890123456789";
  ASSERT_GT(strlen(long_name), (size_t)ZX_MAX_NAME_LEN - 1, "too short to truncate");

  zx_handle_t proc;
  zx_handle_t vmar;
  ASSERT_OK(zx_process_create(zx_job_default(), long_name, sizeof(long_name), 0, &proc, &vmar));
  static char proc_name[ZX_MAX_NAME_LEN];
  ASSERT_OK(zx_object_get_property(proc, ZX_PROP_NAME, proc_name, ZX_MAX_NAME_LEN));
  ASSERT_EQ(strncmp(proc_name, long_name, ZX_MAX_NAME_LEN - 1), 0);
  ASSERT_OK(zx_handle_close(vmar));
  ASSERT_OK(zx_handle_close(proc));
}

TEST(ProcessTest, EmptyNameSucceeds) {
  // Creating a process with "" name, 0 name_len should succeed.
  zx_handle_t proc;
  zx_handle_t vmar;
  ASSERT_OK(zx_process_create(zx_job_default(), "", 0, 0, &proc, &vmar));
  static char proc_name[ZX_MAX_NAME_LEN];
  ASSERT_OK(zx_object_get_property(proc, ZX_PROP_NAME, proc_name, ZX_MAX_NAME_LEN));
  ASSERT_EQ(strcmp(proc_name, ""), 0);
  ASSERT_OK(zx_handle_close(vmar));
  ASSERT_OK(zx_handle_close(proc));
}

TEST(ProcessTest, GetRuntimeNoPermission) {
  zx::process proc;
  zx::vmar vmar;
  ASSERT_OK(zx::process::create(*zx::job::default_job(), "", 0, 0, &proc, &vmar));

  zx_info_handle_basic basic;
  ASSERT_OK(proc.get_info(ZX_INFO_HANDLE_BASIC, &basic, sizeof(basic), nullptr, nullptr));

  zx::process proc_dup;
  ASSERT_OK(proc.duplicate(basic.rights & ~ZX_RIGHT_INSPECT, &proc_dup));
  zx_info_task_runtime_t info;
  ASSERT_OK(proc.get_info(ZX_INFO_TASK_RUNTIME, &info, sizeof(info), nullptr, nullptr));
  ASSERT_EQ(proc_dup.get_info(ZX_INFO_TASK_RUNTIME, &info, sizeof(info), nullptr, nullptr),
            ZX_ERR_ACCESS_DENIED);
}

TEST(ProcessTest, MiniProcessSanity) {
  zx_handle_t proc;
  zx_handle_t thread;
  zx_handle_t vmar;

  ASSERT_OK(zx_process_create(zx_job_default(), "mini-p", 3u, 0, &proc, &vmar));
  ASSERT_OK(zx_thread_create(proc, "mini-p", 2u, 0u, &thread));

  zx_handle_t event;
  ASSERT_OK(zx_event_create(0u, &event));

  zx_handle_t cmd_channel;
  EXPECT_OK(start_mini_process_etc(proc, thread, vmar, event, true, &cmd_channel));

  EXPECT_OK(mini_process_cmd(cmd_channel, MINIP_CMD_ECHO_MSG, nullptr));

  zx_handle_t oev;
  EXPECT_OK(mini_process_cmd(cmd_channel, MINIP_CMD_CREATE_EVENT, &oev));

  EXPECT_EQ(mini_process_cmd(cmd_channel, MINIP_CMD_EXIT_NORMAL, nullptr), ZX_ERR_PEER_CLOSED);

  zx_handle_close(thread);
  zx_handle_close(proc);
  zx_handle_close(vmar);
}

TEST(ProcessTest, ProcessStartNoHandle) {
  zx_handle_t proc;
  zx_handle_t thread;
  zx_handle_t vmar;

  constexpr const char kTestName[] = "test-no-handles";
  ASSERT_OK(zx_process_create(zx_job_default(), kTestName, sizeof(kTestName) - 1, 0, &proc, &vmar));
  ASSERT_OK(zx_thread_create(proc, kTestName, sizeof(kTestName) - 1, 0u, &thread));

  // The process will get no handles, but it can still make syscalls.
  // The vDSO's e_entry points to zx_process_exit.  So the process will
  // enter at `zx_process_exit(ZX_HANDLE_INVALID);`.
  uintptr_t entry;
  EXPECT_OK(mini_process_load_vdso(proc, vmar, nullptr, &entry));

  // The vDSO ABI needs a stack, though zx_process_exit actually might not.
  uintptr_t stack_base, sp;
  EXPECT_OK(mini_process_load_stack(vmar, false, &stack_base, &sp));
  zx_handle_close(vmar);

  EXPECT_OK(zx_process_start(proc, thread, entry, sp, ZX_HANDLE_INVALID, 0));
  zx_handle_close(thread);

  zx_signals_t signals;
  EXPECT_OK(zx_object_wait_one(proc, ZX_TASK_TERMINATED, zx_deadline_after(ZX_SEC(1)), &signals));
  EXPECT_EQ(signals, ZX_TASK_TERMINATED);

  zx_info_process_t info{};
  EXPECT_OK(zx_object_get_info(proc, ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(info.return_code, int64_t{ZX_HANDLE_INVALID});

  zx_handle_close(proc);
}

#if defined(__x86_64__)

#include <cpuid.h>

// This is based on code from kernel/ which isn't usable by code in system/.
enum { X86_CPUID_ADDR_WIDTH = 0x80000008 };

static uint32_t x86_linear_address_width() {
  uint32_t eax, ebx, ecx, edx;
  __cpuid(X86_CPUID_ADDR_WIDTH, eax, ebx, ecx, edx);
  return (eax >> 8) & 0xff;
}

#endif

TEST(ProcessTest, ProcessStartNonUserspaceEntry) {
  auto test_process_start = [&](uintptr_t entry, zx_status_t expected) {
    zx_handle_t proc;
    zx_handle_t thread;
    zx_handle_t vmar;

    constexpr const char kTestName[] = "test-noncanonical-entry";
    ASSERT_OK(
        zx_process_create(zx_job_default(), kTestName, sizeof(kTestName) - 1, 0, &proc, &vmar));
    zx_handle_close(vmar);
    ASSERT_OK(zx_thread_create(proc, kTestName, sizeof(kTestName) - 1, 0u, &thread));

    char stack[1024] __ALIGNED(16);  // a small stack for the process.
    uintptr_t sp = reinterpret_cast<uintptr_t>(&stack[1024]);

    EXPECT_EQ(expected, zx_process_start(proc, thread, entry, sp, ZX_HANDLE_INVALID, 0));
    zx_handle_close(thread);
    zx_handle_close(proc);
  };

  uintptr_t non_user_pc = 0x1UL;
  uintptr_t kernel_pc = 0xffffff8000000000UL;

  test_process_start(non_user_pc, ZX_ERR_INVALID_ARGS);
  test_process_start(kernel_pc, ZX_ERR_INVALID_ARGS);

#if defined(__x86_64__)
  uintptr_t non_canonical_pc = ((uintptr_t)1) << (x86_linear_address_width() - 1);
  test_process_start(non_canonical_pc, ZX_ERR_INVALID_ARGS);
#endif  // defined(__x86_64__)
}

TEST(ProcessTest, ProcessStartFail) {
  zx_handle_t event1, event2;
  zx_handle_t process;
  zx_handle_t thread;

  ASSERT_OK(zx_event_create(0u, &event1));
  ASSERT_OK(zx_event_create(0u, &event2));

  ASSERT_OK(start_mini_process(zx_job_default(), event1, &process, &thread));

  zx_handle_t other_thread;
  ASSERT_OK(zx_thread_create(process, "test", 4u, 0, &other_thread));

  // Test that calling process_start() again for an existing process fails in a
  // reasonable way. Also test that the transferred object is closed.
  EXPECT_EQ(zx_process_start(process, other_thread, 0, 0, event2, 0), ZX_ERR_BAD_STATE);
  EXPECT_EQ(zx_object_signal(event2, 0u, ZX_EVENT_SIGNALED), ZX_ERR_BAD_HANDLE);

  zx_handle_close(process);
  zx_handle_close(thread);
  zx_handle_close(other_thread);
}

TEST(ProcessTest, ProcessNotKilledViaThreadClose) {
  zx_handle_t event;
  ASSERT_OK(zx_event_create(0u, &event));

  zx_handle_t process;
  zx_handle_t thread;
  ASSERT_OK(start_mini_process(zx_job_default(), event, &process, &thread));

  EXPECT_OK(zx_handle_close(thread));

  // The timeout below does not have to be large because the processing happens
  // synchronously if indeed |thread| is the last handle.
  zx_signals_t signals = 0;
  EXPECT_EQ(
      zx_object_wait_one(process, ZX_TASK_TERMINATED, zx_deadline_after(ZX_MSEC(1)), &signals),
      ZX_ERR_TIMED_OUT);
  EXPECT_NE(signals, ZX_TASK_TERMINATED);

  EXPECT_OK(zx_handle_close(process));
}

TEST(ProcessTest, ProcessNotKilledViaProcessClose) {
  zx_handle_t event;
  ASSERT_OK(zx_event_create(0u, &event));

  zx_handle_t process;
  zx_handle_t thread;
  ASSERT_OK(start_mini_process(zx_job_default(), event, &process, &thread));

  EXPECT_OK(zx_handle_close(process));

  // The timeout below does not have to be large because the processing happens
  // synchronously if indeed |process| is the last handle.
  zx_signals_t signals;
  EXPECT_EQ(zx_object_wait_one(thread, ZX_TASK_TERMINATED, zx_deadline_after(ZX_MSEC(1)), &signals),
            ZX_ERR_TIMED_OUT);

  EXPECT_OK(zx_handle_close(thread));
}

TEST(ProcessTest, KillProcessViaThreadKill) {
  zx_handle_t event;
  ASSERT_OK(zx_event_create(0u, &event));

  zx_handle_t process;
  zx_handle_t thread;
  ASSERT_OK(start_mini_process(zx_job_default(), event, &process, &thread));

  // Killing the only thread should cause the process to terminate.
  EXPECT_OK(zx_task_kill(thread));

  zx_signals_t signals;
  EXPECT_OK(zx_object_wait_one(process, ZX_TASK_TERMINATED, ZX_TIME_INFINITE, &signals));
  EXPECT_EQ(signals, ZX_TASK_TERMINATED);

  EXPECT_OK(zx_handle_close(process));
  EXPECT_OK(zx_handle_close(thread));
}

zx_status_t dup_send_handle(zx_handle_t channel, zx_handle_t handle) {
  zx_handle_t dup;
  zx_status_t st = zx_handle_duplicate(handle, ZX_RIGHT_SAME_RIGHTS, &dup);
  if (st < 0)
    return st;
  return zx_channel_write(channel, 0u, nullptr, 0u, &dup, 1u);
}

TEST(ProcessTest, KillChannelHandleCycle) {
  zx_handle_t chan[2] = {ZX_HANDLE_INVALID, ZX_HANDLE_INVALID};
  ASSERT_OK(zx_channel_create(0u, &chan[0], &chan[1]));

  zx_handle_t proc1, proc2;
  zx_handle_t vmar1, vmar2;

  zx_handle_t job_child;
  ASSERT_OK(zx_job_create(zx_job_default(), 0u, &job_child));

  ASSERT_OK(zx_process_create(job_child, "ttp1", 4u, 0u, &proc1, &vmar1));
  ASSERT_OK(zx_process_create(job_child, "ttp2", 4u, 0u, &proc2, &vmar2));

  zx_handle_t thread1, thread2;

  ASSERT_OK(zx_thread_create(proc1, "th1", 3u, 0u, &thread1));
  ASSERT_OK(zx_thread_create(proc2, "th2", 3u, 0u, &thread2));

  // Now we stuff duplicated process and thread handles into each side of the channel.
  EXPECT_OK(dup_send_handle(chan[0], proc2));
  EXPECT_OK(dup_send_handle(chan[0], thread2));

  EXPECT_OK(dup_send_handle(chan[1], proc1));
  EXPECT_OK(dup_send_handle(chan[1], thread1));

  // The process start with each one side of the channel. We don't have access to the
  // channel anymore.

  zx_handle_t minip_chn[2];

  EXPECT_OK(start_mini_process_etc(proc1, thread1, vmar1, chan[0], true, &minip_chn[0]));
  EXPECT_OK(start_mini_process_etc(proc2, thread2, vmar2, chan[1], true, &minip_chn[1]));

  EXPECT_OK(zx_handle_close(vmar2));
  EXPECT_OK(zx_handle_close(vmar1));

  EXPECT_OK(zx_handle_close(proc1));
  EXPECT_OK(zx_handle_close(proc2));

  // Make (relatively) certain the processes are alive.

  zx_signals_t signals;
  EXPECT_EQ(
      zx_object_wait_one(thread1, ZX_TASK_TERMINATED, zx_deadline_after(kTimeoutNs), &signals),
      ZX_ERR_TIMED_OUT);

  EXPECT_EQ(
      zx_object_wait_one(thread2, ZX_TASK_TERMINATED, zx_deadline_after(kTimeoutNs), &signals),
      ZX_ERR_TIMED_OUT);

  // At this point the two processes have each other thread/process handles.
  EXPECT_OK(zx_handle_close(thread1));

  EXPECT_EQ(
      zx_object_wait_one(thread2, ZX_TASK_TERMINATED, zx_deadline_after(kTimeoutNs), &signals),
      ZX_ERR_TIMED_OUT);

  // The only way out of this situation is to use the job handle.
  EXPECT_OK(zx_task_kill(job_child));

  EXPECT_OK(zx_object_wait_one(thread2, ZX_TASK_TERMINATED, ZX_TIME_INFINITE, &signals));
  signals &= ZX_TASK_TERMINATED;
  EXPECT_EQ(signals, ZX_TASK_TERMINATED);

  EXPECT_OK(zx_handle_close(thread2));
  EXPECT_OK(zx_handle_close(job_child));
}

// Tests that |zx_info_process_t| fields reflect the current state of a process.
TEST(ProcessTest, InfoReflectsProcessState) {
  // Create a process with one thread.
  zx_handle_t event;
  ASSERT_OK(zx_event_create(0u, &event));

  zx_handle_t job_child;
  ASSERT_OK(zx_job_create(zx_job_default(), 0u, &job_child));

  zx_handle_t proc;
  zx_handle_t vmar;
  ASSERT_OK(zx_process_create(job_child, "ttp", 4u, 0u, &proc, &vmar));

  zx_handle_t thread;
  ASSERT_OK(zx_thread_create(proc, "th", 3u, 0u, &thread));

  zx_info_process_t info;
  ASSERT_OK(zx_object_get_info(proc, ZX_INFO_PROCESS, &info, sizeof(info), NULL, NULL));
  EXPECT_FALSE(info.started, "process should not appear as started");
  EXPECT_FALSE(info.exited, "process should not appear as exited");
  EXPECT_EQ(info.return_code, 0, "return code is zero");

  zx_handle_t minip_chn;
  // Start the process and make (relatively) certain it's alive.
  ASSERT_OK(start_mini_process_etc(proc, thread, vmar, event, true, &minip_chn));
  zx_signals_t signals;
  ASSERT_EQ(zx_object_wait_one(proc, ZX_TASK_TERMINATED, zx_deadline_after(kTimeoutNs), &signals),
            ZX_ERR_TIMED_OUT);

  ASSERT_OK(zx_object_get_info(proc, ZX_INFO_PROCESS, &info, sizeof(info), NULL, NULL));
  EXPECT_TRUE(info.started, "process should appear as started");
  EXPECT_FALSE(info.exited, "process should not appear as exited");

  // Kill the process and wait for it to terminate.
  ASSERT_OK(zx_task_kill(proc));
  ASSERT_OK(zx_object_wait_one(proc, ZX_TASK_TERMINATED, ZX_TIME_INFINITE, &signals));
  ASSERT_EQ(signals, ZX_TASK_TERMINATED);

  ASSERT_OK(zx_object_get_info(proc, ZX_INFO_PROCESS, &info, sizeof(info), NULL, NULL));
  EXPECT_TRUE(info.started, "process should appear as started");
  EXPECT_TRUE(info.exited, "process should appear as exited");
  EXPECT_EQ(info.return_code, ZX_TASK_RETCODE_SYSCALL_KILL, "process retcode invalid");
}

// Helper class to encapsulate starting a process with up to kNumThreads no-op child threads.
class TestProcess {
 public:
  static constexpr int kMaxThreads = 3;

  // Creates the process handle, must be called first before any other function.
  void CreateProcess() {
    constexpr const char* kProcessName = "test_process";
    EXPECT_OK(zx_process_create(zx_job_default(), kProcessName, strlen(kProcessName), 0, &process_,
                                &vmar_));
  }

  // Creates a child thread but does not start it.
  void CreateThread() {
    ASSERT_LT(num_threads_, kMaxThreads);

    zx_handle_t thread;
    char name[32];
    size_t name_length = snprintf(name, sizeof(name), "test_thread_%d", num_threads_);
    ASSERT_OK(zx_thread_create(process_, name, name_length, 0, &thread));

    threads_[num_threads_++] = thread;
  }

  // Starts the process and all child threads.
  void StartProcess() {
    ASSERT_GT(num_threads_, 0);

    // The first thread must start the process.
    // We don't use this event but starting a new process requires passing it a handle.
    zx_handle_t event = ZX_HANDLE_INVALID;
    ASSERT_OK(zx_event_create(0u, &event));
    ASSERT_OK(start_mini_process_etc(process_, threads_[0], vmar_, event, true, nullptr));

    for (int i = 1; i < num_threads_; ++i) {
      ASSERT_OK(start_mini_process_thread(threads_[i], vmar_));
    }
  }

  // Waits for a signal on the requested thread and returns true if the result
  // matches |expected|.
  //
  // If |expected| is ZX_ERR_TIMED_OUT this waits for a finite amount of time,
  // otherwise it waits forever.
  bool WaitForThreadSignal(int index, zx_signals_t signal, zx_status_t expected) {
    zx_time_t timeout = ZX_TIME_INFINITE;
    if (expected == ZX_ERR_TIMED_OUT)
      timeout = zx_deadline_after(kTimeoutNs);

    return zx_object_wait_one(threads_[index], signal, timeout, nullptr) == expected;
  }

  // Do this explicitly rather than in the destructor to catch any errors.
  void StopProcess() {
    EXPECT_OK(zx_task_kill(process_));
    EXPECT_OK(zx_handle_close(process_));
    EXPECT_OK(zx_handle_close(vmar_));
    EXPECT_OK(zx_handle_close_many(threads_, num_threads_));
  }

  zx_handle_t process() const { return process_; }
  zx_handle_t thread(int index) const { return threads_[index]; }

 private:
  zx_handle_t process_ = ZX_HANDLE_INVALID;
  zx_handle_t vmar_ = ZX_HANDLE_INVALID;

  int num_threads_ = 0;
  zx_handle_t threads_[kMaxThreads];
};

TEST(ProcessTest, Suspend) {
  TestProcess test_process;
  ASSERT_NO_FAILURES(test_process.CreateProcess());
  ASSERT_NO_FAILURES(test_process.CreateThread());
  ASSERT_NO_FAILURES(test_process.StartProcess());

  zx_handle_t suspend_token;
  ASSERT_OK(zx_task_suspend(test_process.process(), &suspend_token));
  ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_SUSPENDED, ZX_OK));

  ASSERT_OK(zx_handle_close(suspend_token));
  ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_OK));

  ASSERT_NO_FAILURES(test_process.StopProcess());
}

TEST(ProcessTest, SuspendSelf) {
  zx_handle_t suspend_token;
  EXPECT_EQ(zx_task_suspend(zx_process_self(), &suspend_token), ZX_ERR_NOT_SUPPORTED);
}

TEST(ProcessTest, SuspendMultipleThreads) {
  TestProcess test_process;
  ASSERT_NO_FAILURES(test_process.CreateProcess());
  ASSERT_NO_FAILURES(test_process.CreateThread());
  ASSERT_NO_FAILURES(test_process.CreateThread());
  ASSERT_NO_FAILURES(test_process.CreateThread());
  ASSERT_NO_FAILURES(test_process.StartProcess());

  zx_handle_t suspend_token;
  ASSERT_OK(zx_task_suspend(test_process.process(), &suspend_token));
  ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_SUSPENDED, ZX_OK));
  ASSERT_TRUE(test_process.WaitForThreadSignal(1, ZX_THREAD_SUSPENDED, ZX_OK));
  ASSERT_TRUE(test_process.WaitForThreadSignal(2, ZX_THREAD_SUSPENDED, ZX_OK));

  ASSERT_OK(zx_handle_close(suspend_token));
  ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_OK));
  ASSERT_TRUE(test_process.WaitForThreadSignal(1, ZX_THREAD_RUNNING, ZX_OK));
  ASSERT_TRUE(test_process.WaitForThreadSignal(2, ZX_THREAD_RUNNING, ZX_OK));

  ASSERT_NO_FAILURES(test_process.StopProcess());
}

TEST(ProcessTest, SuspendBeforeCreatingThreads) {
  TestProcess test_process;
  ASSERT_NO_FAILURES(test_process.CreateProcess());

  zx_handle_t suspend_token;
  ASSERT_OK(zx_task_suspend(test_process.process(), &suspend_token));

  ASSERT_NO_FAILURES(test_process.CreateThread());
  ASSERT_NO_FAILURES(test_process.StartProcess());
  ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_SUSPENDED, ZX_OK));

  ASSERT_OK(zx_handle_close(suspend_token));
  ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_OK));

  ASSERT_NO_FAILURES(test_process.StopProcess());
}

TEST(ProcessTest, SuspendBeforeStartingThreads) {
  TestProcess test_process;
  ASSERT_NO_FAILURES(test_process.CreateProcess());
  ASSERT_NO_FAILURES(test_process.CreateThread());

  zx_handle_t suspend_token;
  ASSERT_OK(zx_task_suspend(test_process.process(), &suspend_token));

  ASSERT_NO_FAILURES(test_process.StartProcess());
  ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_SUSPENDED, ZX_OK));

  ASSERT_OK(zx_handle_close(suspend_token));
  ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_OK));

  ASSERT_NO_FAILURES(test_process.StopProcess());
}

TEST(ProcessTest, SuspendProcessThenThread) {
  TestProcess test_process;
  ASSERT_NO_FAILURES(test_process.CreateProcess());
  ASSERT_NO_FAILURES(test_process.CreateThread());
  ASSERT_NO_FAILURES(test_process.StartProcess());

  zx_handle_t process_suspend_token;
  ASSERT_OK(zx_task_suspend(test_process.process(), &process_suspend_token));
  ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_SUSPENDED, ZX_OK));

  zx_handle_t thread_suspend_token;
  ASSERT_OK(zx_task_suspend(test_process.thread(0), &thread_suspend_token));

  // When we release the process token, the thread should remain suspended.
  ASSERT_OK(zx_handle_close(process_suspend_token));
  ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_ERR_TIMED_OUT));

  // Now close the thread token and it should resume.
  ASSERT_OK(zx_handle_close(thread_suspend_token));
  ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_OK));

  ASSERT_NO_FAILURES(test_process.StopProcess());
}

TEST(ProcessTest, SuspendThreadThenProcess) {
  TestProcess test_process;
  ASSERT_NO_FAILURES(test_process.CreateProcess());
  ASSERT_NO_FAILURES(test_process.CreateThread());
  ASSERT_NO_FAILURES(test_process.StartProcess());

  zx_handle_t thread_suspend_token;
  ASSERT_OK(zx_task_suspend(test_process.thread(0), &thread_suspend_token));
  ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_SUSPENDED, ZX_OK));

  zx_handle_t process_suspend_token;
  ASSERT_OK(zx_task_suspend(test_process.process(), &process_suspend_token));

  ASSERT_OK(zx_handle_close(process_suspend_token));
  ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_ERR_TIMED_OUT));

  ASSERT_OK(zx_handle_close(thread_suspend_token));
  ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_OK));

  ASSERT_NO_FAILURES(test_process.StopProcess());
}

TEST(ProcessTest, SuspendThreadAndProcessBeforeStartingProcess) {
  TestProcess test_process;

  // Create and immediately suspend the process and thread.
  ASSERT_NO_FAILURES(test_process.CreateProcess());
  zx_handle_t process_suspend_token;
  ASSERT_OK(zx_task_suspend(test_process.process(), &process_suspend_token));

  ASSERT_NO_FAILURES(test_process.CreateThread());
  zx_handle_t thread_suspend_token;
  ASSERT_OK(zx_task_suspend(test_process.thread(0), &thread_suspend_token));

  ASSERT_NO_FAILURES(test_process.StartProcess());
  ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_SUSPENDED, ZX_OK));

  // Resume the process, thread should stay suspended.
  ASSERT_OK(zx_handle_close(process_suspend_token));
  ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_ERR_TIMED_OUT));

  ASSERT_OK(zx_handle_close(thread_suspend_token));
  ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_OK));

  ASSERT_NO_FAILURES(test_process.StopProcess());
}

TEST(ProcessTest, SuspendTwice) {
  TestProcess test_process;
  ASSERT_NO_FAILURES(test_process.CreateProcess());
  ASSERT_NO_FAILURES(test_process.CreateThread());
  ASSERT_NO_FAILURES(test_process.StartProcess());

  zx_handle_t suspend_tokens[2];
  ASSERT_OK(zx_task_suspend(test_process.process(), &suspend_tokens[0]));
  ASSERT_OK(zx_task_suspend(test_process.process(), &suspend_tokens[1]));
  ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_SUSPENDED, ZX_OK));

  ASSERT_OK(zx_handle_close(suspend_tokens[0]));
  ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_ERR_TIMED_OUT));

  ASSERT_OK(zx_handle_close(suspend_tokens[1]));
  ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_OK));

  ASSERT_NO_FAILURES(test_process.StopProcess());
}

TEST(ProcessTest, SuspendTwiceBeforeCreatingThreads) {
  TestProcess test_process;
  ASSERT_NO_FAILURES(test_process.CreateProcess());

  zx_handle_t suspend_tokens[2];
  ASSERT_OK(zx_task_suspend(test_process.process(), &suspend_tokens[0]));
  ASSERT_OK(zx_task_suspend(test_process.process(), &suspend_tokens[1]));

  ASSERT_NO_FAILURES(test_process.CreateThread());
  ASSERT_NO_FAILURES(test_process.StartProcess());
  ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_SUSPENDED, ZX_OK));

  ASSERT_OK(zx_handle_close(suspend_tokens[0]));
  ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_ERR_TIMED_OUT));

  ASSERT_OK(zx_handle_close(suspend_tokens[1]));
  ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_OK));

  ASSERT_NO_FAILURES(test_process.StopProcess());
}

// This test isn't super reliable since it has to try to suspend and resume while a thread is in
// the small window while it's dying but before it's dead, but there doesn't seem to be a way
// to deterministically hit that window so unfortunately this is the best we can do.
//
// In the expected case this test will always succeed, but if there is an underlying bug it
// will occasionally fail, so if this test begins to show flakiness it likely represents a real
// bug.
TEST(ProcessTest, SuspendWithDyingThread) {
  TestProcess test_process;
  ASSERT_NO_FAILURES(test_process.CreateProcess());
  ASSERT_NO_FAILURES(test_process.CreateThread());
  ASSERT_NO_FAILURES(test_process.CreateThread());
  ASSERT_NO_FAILURES(test_process.CreateThread());
  ASSERT_NO_FAILURES(test_process.StartProcess());

  // Kill the middle thread.
  ASSERT_OK(zx_task_kill(test_process.thread(1)));

  // Now suspend the process and make sure it still works on the live threads.
  zx_handle_t suspend_token;
  ASSERT_OK(zx_task_suspend(test_process.process(), &suspend_token));
  ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_SUSPENDED, ZX_OK));
  ASSERT_TRUE(test_process.WaitForThreadSignal(2, ZX_THREAD_SUSPENDED, ZX_OK));

  ASSERT_OK(zx_handle_close(suspend_token));
  ASSERT_TRUE(test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_OK));
  ASSERT_TRUE(test_process.WaitForThreadSignal(2, ZX_THREAD_RUNNING, ZX_OK));

  ASSERT_NO_FAILURES(test_process.StopProcess());
}

TEST(ProcessTest, GetTaskRuntime) {
  TestProcess test_process;
  ASSERT_NO_FAILURES(test_process.CreateProcess());
  ASSERT_NO_FAILURES(test_process.CreateThread());

  // Get info before the threads start running.
  zx_info_task_runtime_t info;
  ASSERT_OK(zx_object_get_info(test_process.process(), ZX_INFO_TASK_RUNTIME, &info, sizeof(info),
                               nullptr, nullptr));
  ASSERT_EQ(info.cpu_time, 0);
  ASSERT_EQ(info.queue_time, 0);

  ASSERT_NO_FAILURES(test_process.StartProcess());

  test_process.WaitForThreadSignal(0, ZX_THREAD_RUNNING, ZX_OK);

  // We are occasionally fast enough reading the thread info to see it before it gets scheduled.
  // Loop until we see the values we are looking for.
  while (info.cpu_time == 0 || info.queue_time == 0) {
    ASSERT_OK(zx_object_get_info(test_process.process(), ZX_INFO_TASK_RUNTIME, &info, sizeof(info),
                                 nullptr, nullptr));
  }

  EXPECT_GT(info.cpu_time, 0);
  EXPECT_GT(info.queue_time, 0);

  ASSERT_OK(zx_task_kill(test_process.process()));
  ASSERT_OK(
      zx_object_wait_one(test_process.process(), ZX_TASK_TERMINATED, ZX_TIME_INFINITE, nullptr));

  // Read info after process death, ensure it does not change.
  ASSERT_OK(zx_object_get_info(test_process.process(), ZX_INFO_TASK_RUNTIME, &info, sizeof(info),
                               nullptr, nullptr));
  EXPECT_GT(info.cpu_time, 0);
  EXPECT_GT(info.queue_time, 0);

  zx_info_task_runtime_t info2;
  ASSERT_OK(zx_object_get_info(test_process.process(), ZX_INFO_TASK_RUNTIME, &info2, sizeof(info2),
                               nullptr, nullptr));
  EXPECT_EQ(info.cpu_time, info2.cpu_time);
  EXPECT_EQ(info.queue_time, info2.queue_time);

  ASSERT_NO_FAILURES(test_process.StopProcess());
}

// A stress test designed to create a race where one thread is creating a process while another
// thread is killing its parent job.
TEST(ProcessTest, CreateAndKillJobRaceStress) {
  constexpr zx_duration_t kTestDuration = ZX_SEC(1);
  srand(4);

  struct args_t {
    std::atomic<bool>* keep_running;
    std::atomic<zx_handle_t>* job;
  };

  // Repeatedly create and kill a job.
  auto killer_thread = [](void* arg) -> int {
    auto [job, keep_running] = *reinterpret_cast<args_t*>(arg);
    while (keep_running->load()) {
      zx_handle_t handle = ZX_HANDLE_INVALID;
      zx_status_t status = zx_job_create(zx_job_default(), 0, &handle);
      if (status != ZX_OK) {
        return status;
      }
      job->store(handle);

      // Give the creator threads an opportunity to get the handle before killing the job.
      zx_nanosleep(ZX_MSEC(10));

      status = zx_task_kill(handle);
      if (status != ZX_OK) {
        return status;
      }
      zx_handle_close(handle);
      handle = ZX_HANDLE_INVALID;
      job->store(handle);
    }
    return ZX_OK;
  };

  // Repeatedly create a process.
  auto creator_thread = [](void* arg) -> int {
    auto [job, keep_running] = *reinterpret_cast<args_t*>(arg);
    constexpr const char kName[] = "create-and-kill";
    while (keep_running->load()) {
      zx_handle_t handle = job->load();
      if (handle == ZX_HANDLE_INVALID) {
        continue;
      }

      zx_handle_t proc = ZX_HANDLE_INVALID;
      zx_handle_t vmar = ZX_HANDLE_INVALID;
      zx_status_t status =
          zx_process_create(handle, "create-and-kill", sizeof(kName) - 1, 0, &proc, &vmar);

      // We're racing with the killer_thread so it's entirely possible for zx_process_create
      // to fail with ZX_ERR_BAD_HANDLE or ZX_ERR_BAD_STATE. Just ignore those.
      if (status != ZX_OK && status != ZX_ERR_BAD_HANDLE && status != ZX_ERR_BAD_STATE) {
        return status;
      }
      zx_handle_close(proc);
      proc = ZX_HANDLE_INVALID;
      zx_handle_close(vmar);
      vmar = ZX_HANDLE_INVALID;
    }

    return ZX_OK;
  };

  std::atomic<bool> keep_running(true);
  std::atomic<zx_handle_t> job(ZX_HANDLE_INVALID);
  args_t args{&keep_running, &job};

  thrd_t killer;
  ASSERT_EQ(thrd_create(&killer, killer_thread, &args), thrd_success);

  constexpr unsigned kNumCreators = 4;
  thrd_t creators[kNumCreators];
  for (auto& t : creators) {
    ASSERT_EQ(thrd_create(&t, creator_thread, &args), thrd_success);
  }

  zx_nanosleep(zx_deadline_after(kTestDuration));

  keep_running.store(false);
  for (auto& t : creators) {
    int res;
    ASSERT_EQ(thrd_join(t, &res), thrd_success);
    ASSERT_OK(res);
  }

  int res;
  ASSERT_EQ(thrd_join(killer, &res), thrd_success);
  ASSERT_OK(res);

  zx_handle_close(args.job->load());
}

TEST(ProcessTest, ProcessStartWriteThreadState) {
  zx_handle_t proc;
  zx_handle_t vmar;
  ASSERT_OK(zx_process_create(zx_job_default(), "ttp", 3u, 0, &proc, &vmar));

  zx_handle_t thread;
  ASSERT_OK(zx_thread_create(proc, "th", 2u, 0u, &thread));

  // Suspend the thread before it starts.
  zx_handle_t token;
  ASSERT_OK(zx_task_suspend(thread, &token));

  zx_handle_t event;
  ASSERT_OK(zx_event_create(0u, &event));

  zx_handle_t minip_chn;
  ASSERT_OK(start_mini_process_etc(proc, thread, vmar, event, false, &minip_chn));

  // Get a known word into memory to point the thread pointer at.  It would
  // be simpler and sufficient for the purpose of this test just to check
  // the value of the thread register itself for a known bit pattern.  But
  // on older x86 hardware there is no unprivileged way to read the register
  // directly (rdfsbase) and it can only be used in a memory access.
  const uintptr_t kCheckValue = MINIP_THREAD_POINTER_CHECK_VALUE;
  zx_handle_t vmo;
  ASSERT_OK(zx_vmo_create(PAGE_SIZE, 0, &vmo));
  ASSERT_OK(zx_vmo_write(vmo, &kCheckValue, 0, sizeof(kCheckValue)));
  uintptr_t addr;
  ASSERT_OK(zx_vmar_map(vmar, ZX_VM_PERM_READ, 0, vmo, 0, PAGE_SIZE, &addr));
  EXPECT_OK(zx_handle_close(vmo));

  // Wait for the new thread to reach quiescent suspended state.
  zx_signals_t signals;
  EXPECT_OK(zx_object_wait_one(thread, ZX_THREAD_SUSPENDED, ZX_TIME_INFINITE, &signals));
  EXPECT_TRUE(signals & ZX_THREAD_SUSPENDED);

  // Fetch the initial register state.
  zx_thread_state_general_regs_t regs;
  ASSERT_OK(zx_thread_read_state(thread, ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)));
  EXPECT_EQ(regs.*kThreadRegister, 0);

  // Write it back with the thread register pointed at our memory.
  regs.*kThreadRegister = addr;
  ASSERT_OK(zx_thread_write_state(thread, ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)));

  // Now let the thread run again.
  EXPECT_OK(zx_handle_close(token));

  // Complete the startup handshake that had to be delayed while the thread
  // was suspended.
  EXPECT_OK(mini_process_wait_for_ack(minip_chn));

  // Now have it read from its thread pointer and check the value.
  EXPECT_OK(mini_process_cmd(minip_chn, MINIP_CMD_CHECK_THREAD_POINTER, nullptr));

  // All done!
  EXPECT_EQ(mini_process_cmd(minip_chn, MINIP_CMD_EXIT_NORMAL, nullptr), ZX_ERR_PEER_CLOSED);

  EXPECT_OK(zx_handle_close(proc));
  EXPECT_OK(zx_handle_close(vmar));
  EXPECT_OK(zx_handle_close(thread));
}

// This checks for lock ordering violations between the acquiring the process dispatcher lock and
// the process handle table lock.
//
// Given that the 'standard' lock ordering is handle table and then dispatcher, this is really
// testing that ZX_INFO_PROCESS_VMOS doesn't acquire in the other order.
//
// object_wait_async and port_cancel are used as syscalls that will allow us to hold the handle
// table lock whilst operating on a process in a way that requires grabbing the dispatcher lock.
// This represents the 'correct' ordering.
TEST(ProcessTest, ProcessWaitAsyncCancelSelf) {
  // Start up a thread in a mini-process that is given a copy of the process handle and will
  // create a port and infinitely loop doing process.wait_async(port) + port.cancel(process)
  zx::process process;
  zx::vmar vmar;

  constexpr const char kProcessName[] = "test_process";
  ASSERT_OK(zx::process::create(*zx::job::default_job(), kProcessName, sizeof(kProcessName), 0,
                                &process, &vmar));

  zx::thread thread;

  constexpr const char kThreadName[] = "test_thread";
  ASSERT_OK(zx::thread::create(process, kThreadName, sizeof(kThreadName), 0, &thread));

  zx::channel cntrl_channel;
  zx::process process_dup;
  ASSERT_OK(process.duplicate(ZX_RIGHT_SAME_RIGHTS, &process_dup));
  ASSERT_OK(start_mini_process_etc(process.get(), thread.get(), vmar.get(), process_dup.release(),
                                   true, cntrl_channel.reset_and_get_address()));

  ASSERT_OK(mini_process_cmd_send(cntrl_channel.get(), MINIP_CMD_WAIT_ASYNC_CANCEL));

  // Call get_info several times on the process. We're trying to trigger a race that will cause a
  // kernel deadlock. In testing with the deadlock present 10000 iterations would reliably trigger
  // and does not take very long to run.
  zx_info_vmo_t vmo;
  size_t actual;
  size_t available;
  for (int i = 0; i < 10000; i++) {
    ASSERT_OK(process.get_info(ZX_INFO_PROCESS_VMOS, &vmo, sizeof(vmo), &actual, &available));
  }
  // We need to explicitly kill the process tree as we gave the mini-process a handle to itself,
  // so it is able to keep itself alive when we close our copies of the handles otherwise.
  ASSERT_OK(process.kill());
  zx_signals_t pending;
  ASSERT_OK(process.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), &pending));
}

TEST(ProcessTest, ForbidDestroyRootVmar) {
  zx::process process;
  zx::vmar vmar;

  constexpr const char kProcessName[] = "test_process";
  ASSERT_OK(zx::process::create(*zx::job::default_job(), kProcessName, sizeof(kProcessName), 0,
                                &process, &vmar));

  // Attempt to destroy the vmar. We accept this call either succeeding or not being supported, as
  // long as our future get_info call doesn't cause a kernel panic.
  zx_status_t result = vmar.destroy();
  ASSERT_TRUE(result == ZX_OK || result == ZX_ERR_NOT_SUPPORTED);

  // Query the address space.
  zx_info_maps_t map;
  size_t actual, avail;
  ASSERT_OK(process.get_info(ZX_INFO_PROCESS_MAPS, &map, sizeof(map), &actual, &avail));
}

TEST(ProcessTest, ProcessHwTraceContextIdProperty) {
  // Handle whether or not the needed syscall is enabled.
  // It is only enabled with the "kernel.enable-debugging-syscalls=true"
  // kernel command line argument. Unsupported architectures act as-if the
  // syscall is disabled.
  bool debugging_syscalls_enabled = false;

#ifdef __x86_64__
  {
    zx_status_t status;
    char too_small;
    status = zx_object_get_property(zx_process_self(), ZX_PROP_PROCESS_HW_TRACE_CONTEXT_ID,
                                    &too_small, sizeof(too_small));
    if (status != ZX_ERR_NOT_SUPPORTED) {
      EXPECT_EQ(status, ZX_ERR_BUFFER_TOO_SMALL, "unexpected status: %d", status);
      // If we didn't get ZX_ERR_NOT_SUPPORTED, then the needed support is present and enabled.
      debugging_syscalls_enabled = true;
    }
  }
#endif

  auto supported_read_prop_test = [](const char* test_name) {
    zx_status_t status;
    uintptr_t prop_aspace = 0;
    status = zx_object_get_property(zx_process_self(), ZX_PROP_PROCESS_HW_TRACE_CONTEXT_ID,
                                    &prop_aspace, sizeof(prop_aspace));
    EXPECT_EQ(status, ZX_OK, "%s: zx_object_get_property failed: %d", test_name, status);
    // We can't verify the value, but we can at least check it's reasonable.
    EXPECT_NE(prop_aspace, 0, "%s", test_name);
    EXPECT_EQ(prop_aspace & (PAGE_SIZE - 1), 0, "%s", test_name);
  };
  auto unsupported_read_prop_test = [](const char* test_name) {
    zx_status_t status;
    uintptr_t prop_aspace;
    status = zx_object_get_property(zx_process_self(), ZX_PROP_PROCESS_HW_TRACE_CONTEXT_ID,
                                    &prop_aspace, sizeof(prop_aspace));
    EXPECT_EQ(status, ZX_ERR_NOT_SUPPORTED, "%s: unexpected status: %d", test_name, status);
  };

  fit::function<void(const char*)> read_prop_test;
  printf("Note: debugging syscalls are %s\n", debugging_syscalls_enabled ? "enabled" : "disabled");
  if (debugging_syscalls_enabled) {
    read_prop_test = std::move(supported_read_prop_test);
  } else {
    read_prop_test = std::move(unsupported_read_prop_test);
  }

  // Verify obtaining the context ID works through the stages of process
  // creation/death.
  static const char name[] = "context-id-test";
  {
    zx::process proc;
    zx::vmar vmar;

    ASSERT_OK(
        zx::process::create(*zx::job::default_job(), name, sizeof(name) - 1, 0, &proc, &vmar));
    read_prop_test("process created");

    zx::thread thread;
    ASSERT_OK(zx::thread::create(proc, name, sizeof(name) - 1, 0u, &thread));
    zx::event event;
    ASSERT_OK(zx::event::create(0u, &event));
    zx::channel cmd_channel;
    ASSERT_OK(start_mini_process_etc(proc.get(), thread.get(), vmar.get(), event.get(), true,
                                     cmd_channel.reset_and_get_address()));
    ASSERT_OK(mini_process_cmd(cmd_channel.get(), MINIP_CMD_ECHO_MSG, nullptr));
    read_prop_test("process live");

    ASSERT_EQ(mini_process_cmd(cmd_channel.get(), MINIP_CMD_EXIT_NORMAL, nullptr),
              ZX_ERR_PEER_CLOSED);
    zx_signals_t signals;
    ASSERT_OK(proc.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), &signals));
    ASSERT_EQ(signals, ZX_TASK_TERMINATED);
    read_prop_test("process dead");
  }

  // The property is read-only.
  {
    uintptr_t prop_to_set = 0;
    zx_status_t status = zx_object_set_property(
        zx_process_self(), ZX_PROP_PROCESS_HW_TRACE_CONTEXT_ID, &prop_to_set, sizeof(prop_to_set));
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS, "unexpected status: %d", status);
  }
}

}  // namespace
