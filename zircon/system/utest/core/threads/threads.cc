// Copyright 2016 The Fuchsia Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/test-exceptions/exception-catcher.h>
#include <lib/test-exceptions/exception-handling.h>
#include <lib/zx/clock.h>
#include <lib/zx/event.h>
#include <lib/zx/handle.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <lib/zx/vmo.h>
#include <stddef.h>
#include <unistd.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>

#include <atomic>

#include <mini-process/mini-process.h>
#include <runtime/thread.h>
#include <zxtest/zxtest.h>

#include "register-set.h"
#include "thread-functions/thread-functions.h"

static const char kThreadName[] = "test-thread";

// We have to poll a thread's state as there is no way to wait for it to
// transition states. Wait this amount of time. Generally the thread won't
// take very long so this is a compromise between polling too frequently and
// waiting too long.
constexpr zx_duration_t THREAD_BLOCKED_WAIT_DURATION = ZX_MSEC(1);

static void get_koid(zx_handle_t handle, zx_koid_t* koid) {
  zx_info_handle_basic_t info;
  size_t records_read;
  ASSERT_EQ(
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), &records_read, NULL),
      ZX_OK);
  ASSERT_EQ(records_read, 1u);
  *koid = info.koid;
}

static bool get_thread_info(zx_handle_t thread, zx_info_thread_t* info) {
  return zx_object_get_info(thread, ZX_INFO_THREAD, info, sizeof(*info), NULL, NULL) == ZX_OK;
}

// Suspend the given thread and block until it reaches the suspended state. The suspend token
// is written to the output parameter.
static void suspend_thread_synchronous(zx_handle_t thread, zx_handle_t* suspend_token) {
  ASSERT_EQ(zx_task_suspend_token(thread, suspend_token), ZX_OK);

  zx_signals_t observed = 0u;
  ASSERT_EQ(zx_object_wait_one(thread, ZX_THREAD_SUSPENDED, ZX_TIME_INFINITE, &observed), ZX_OK);
}

// Resume the given thread and block until it reaches the running state.
static void resume_thread_synchronous(zx_handle_t thread, zx_handle_t suspend_token) {
  ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);

  zx_signals_t observed = 0u;
  ASSERT_EQ(zx_object_wait_one(thread, ZX_THREAD_RUNNING, ZX_TIME_INFINITE, &observed), ZX_OK);
}

// Updates the thread state to advance over a software breakpoint instruction, assuming the
// breakpoint was just hit. This does not resume the thread, only updates its state.
static void advance_over_breakpoint(zx_handle_t thread) {
#if defined(__aarch64__)
  // Advance 4 bytes to the next instruction after the debug break.
  zx_thread_state_general_regs regs;
  ASSERT_EQ(zx_thread_read_state(thread, ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)), ZX_OK);
  regs.pc += 4;
  ASSERT_EQ(zx_thread_write_state(thread, ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)),
            ZX_OK);
#elif defined(__x86_64__)
// x86 sets the instruction pointer to the following instruction so needs no update.
#else
#error Not supported on this platform.
#endif
}

// Waits for the exception type excp_type, ignoring exceptions of type ignore_type (these will
// just resume the thread), and issues errors for anything else.
//
// Fills |exception_out| with the resulting exception object.
static void wait_thread_excp_type(zx_handle_t thread, zx_handle_t exception_channel,
                                  uint32_t excp_type, uint32_t ignore_type,
                                  zx_handle_t* exception_out) {
  while (true) {
    ASSERT_EQ(zx_object_wait_one(exception_channel, ZX_CHANNEL_READABLE, ZX_TIME_INFINITE, nullptr),
              ZX_OK);

    zx_exception_info_t info;
    zx_handle_t exception;
    ASSERT_EQ(
        zx_channel_read(exception_channel, 0, &info, &exception, sizeof(info), 1, nullptr, nullptr),
        ZX_OK);

    // Use EXPECTs rather than ASSERTs here so that if something fails
    // we log all the relevant information about the packet contents.
    zx_koid_t koid = ZX_KOID_INVALID;
    get_koid(thread, &koid);
    EXPECT_EQ(info.tid, koid);

    if (info.type != ignore_type) {
      EXPECT_EQ(info.type, excp_type);
      *exception_out = exception;
      break;
    } else {
      uint32_t state = ZX_EXCEPTION_STATE_HANDLED;
      ASSERT_EQ(zx_object_set_property(exception, ZX_PROP_EXCEPTION_STATE, &state, sizeof(state)),
                ZX_OK);
      zx_handle_close(exception);
    }
  }
}

namespace {

// Class to encapsulate the various handles and calculations required to start a thread.
//
// This is only necessary to use directly if you need to do something between creating
// and starting the thread - otherwise just use start_thread() for simplicity.
//
// See comment on start_thread (below) about constraints on the entry point.
class ThreadStarter {
 public:
  void CreateThread(zxr_thread_t* thread_out, zx_handle_t* thread_h, bool start_suspended = false) {
    // TODO: Don't leak these when the thread dies.
    // If the thread should start suspended, give it a 0-size VMO for a stack so
    // that it will crash if it gets to userspace.
    ASSERT_EQ(zx::vmo::create(start_suspended ? 0 : kStackSize, ZX_VMO_RESIZABLE, &stack_handle_),
              ZX_OK);
    ASSERT_NE(stack_handle_.get(), ZX_HANDLE_INVALID);

    ASSERT_EQ(zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0,
                          stack_handle_.get(), 0, kStackSize, &stack_),
              ZX_OK);

    ASSERT_EQ(zxr_thread_create(zx_process_self(), "test_thread", false, thread_out), ZX_OK);
    thread_ = thread_out;

    if (thread_h) {
      ASSERT_EQ(
          zx_handle_duplicate(zxr_thread_get_handle(thread_out), ZX_RIGHT_SAME_RIGHTS, thread_h),
          ZX_OK);
    }
  }

  void GrowStackVmo() { ASSERT_EQ(stack_handle_.set_size(kStackSize), ZX_OK); }

  bool StartThread(zxr_thread_entry_t entry, void* arg) {
    return zxr_thread_start(thread_, stack_, kStackSize, entry, arg) == ZX_OK;
  }

  // Destroy a thread structure that is either created but unstarted or is
  // known to belong to a thread that has been zx_task_kill'd and has not been
  // joined.
  bool DestroyThread() { return zxr_thread_destroy(thread_) == ZX_OK; }

 private:
  static constexpr size_t kStackSize = 256u << 10;

  zx::vmo stack_handle_;

  uintptr_t stack_ = 0u;
  zxr_thread_t* thread_ = nullptr;
};

}  // namespace

// NOTE!  The entry point code here must be built specially so it doesn't
// require full proper ABI setup, which ThreadStarter does not do.  The
// thread-functions.cc functions are fine, since that file is explicitly built
// without instrumentation or fancy ABI features.  Anything else must be
// annotated with __NO_SAFESTACK and not call any other function not so
// annotated, which basically means nothing but the raw vDSO entry points.
static bool start_thread(zxr_thread_entry_t entry, void* arg, zxr_thread_t* thread_out,
                         zx_handle_t* thread_h) {
  ThreadStarter starter;
  starter.CreateThread(thread_out, thread_h);
  return starter.StartThread(entry, arg);
}

// Wait for |thread| to enter blocked state |reason|.
// We wait forever and let Unittest's watchdog handle errors.
static void wait_thread_blocked(zx_handle_t thread, zx_thread_state_t reason) {
  while (true) {
    zx_info_thread_t info;
    ASSERT_TRUE(get_thread_info(thread, &info));
    if (info.state == reason)
      break;
    zx_nanosleep(zx_deadline_after(THREAD_BLOCKED_WAIT_DURATION));
  }
}

static bool CpuMaskBitSet(const zx_cpu_set_t& set, uint32_t i) {
  if (i >= ZX_CPU_SET_MAX_CPUS) {
    return false;
  }
  uint32_t word = i / ZX_CPU_SET_BITS_PER_WORD;
  uint32_t bit = i % ZX_CPU_SET_BITS_PER_WORD;
  return ((set.mask[word] >> bit) & 1u) != 0;
}

TEST(Threads, Basics) {
  zxr_thread_t thread;
  zx_handle_t thread_h;
  ASSERT_TRUE(start_thread(threads_test_sleep_fn, (void*)zx_deadline_after(ZX_MSEC(100)), &thread,
                           &thread_h));
  ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL), ZX_OK);
  ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);
}

TEST(Threads, InvalidRights) {
  zxr_thread_t thread;
  zx_handle_t ro_process_h;

  ASSERT_EQ(zx_handle_duplicate(zx_process_self(), ZX_RIGHT_DESTROY, &ro_process_h), ZX_OK);
  ASSERT_EQ(zxr_thread_create(ro_process_h, "test_thread", false, &thread), ZX_ERR_ACCESS_DENIED);

  ASSERT_EQ(zx_handle_close(ro_process_h), ZX_OK);
}

TEST(Threads, Detach) {
  zxr_thread_t thread;
  zx_handle_t event;
  ASSERT_EQ(zx_event_create(0, &event), ZX_OK);

  zx_handle_t thread_h;
  ASSERT_TRUE(start_thread(threads_test_wait_detach_fn, &event, &thread, &thread_h));
  // We're not detached yet
  ASSERT_FALSE(zxr_thread_detached(&thread));

  ASSERT_EQ(zxr_thread_detach(&thread), ZX_OK);
  ASSERT_TRUE(zxr_thread_detached(&thread));

  // Tell thread to exit
  ASSERT_EQ(zx_object_signal(event, 0, ZX_USER_SIGNAL_0), ZX_OK);

  // Wait for thread to exit
  ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL), ZX_OK);

  ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);
}

TEST(Threads, EmptyNameSucceeds) {
  zx_handle_t thread;
  ASSERT_EQ(zx_thread_create(zx_process_self(), "", 0, 0, &thread), ZX_OK);
  static char thread_name[ZX_MAX_NAME_LEN];
  ASSERT_EQ(zx_object_get_property(thread, ZX_PROP_NAME, thread_name, ZX_MAX_NAME_LEN), ZX_OK);
  ASSERT_EQ(strcmp(thread_name, ""), 0);
  ASSERT_EQ(zx_handle_close(thread), ZX_OK);
}

TEST(Threads, LongNameSucceeds) {
  // Creating a thread with a super long name should succeed.
  static const char long_name[] =
      "0123456789012345678901234567890123456789"
      "0123456789012345678901234567890123456789";
  ASSERT_GT(strlen(long_name), (size_t)ZX_MAX_NAME_LEN - 1, "too short to truncate");

  zxr_thread_t thread;
  ASSERT_EQ(zxr_thread_create(zx_process_self(), long_name, false, &thread), ZX_OK);
  static char thread_name[ZX_MAX_NAME_LEN];
  ASSERT_EQ(zx_object_get_property(zxr_thread_get_handle(&thread), ZX_PROP_NAME, thread_name,
                                   ZX_MAX_NAME_LEN),
            ZX_OK);
  ASSERT_EQ(strncmp(thread_name, long_name, ZX_MAX_NAME_LEN - 1), 0);
  zxr_thread_destroy(&thread);
}

// zx_thread_start() is not supposed to be usable for creating a
// process's first thread.  That's what zx_process_start() is for.
// Check that zx_thread_start() returns an error in this case.
TEST(Threads, ThreadStartOnInitialThread) {
  static const char kProcessName[] = "test-proc-thread1";
  zx_handle_t process;
  zx_handle_t vmar;
  zx_handle_t thread;
  ASSERT_OK(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1, 0, &process,
                              &vmar));
  ASSERT_OK(zx_thread_create(process, kThreadName, sizeof(kThreadName) - 1, 0, &thread));
  ASSERT_EQ(ZX_ERR_BAD_STATE, zx_thread_start(thread, 0, 1, 1, 1));

  ASSERT_OK(zx_handle_close(thread));
  ASSERT_OK(zx_handle_close(vmar));
  ASSERT_OK(zx_handle_close(process));
}

// Test that we don't get an assertion failure (and kernel panic) if we
// pass a zero instruction pointer when starting a thread (in this case via
// zx_thread_create()).
TEST(Threads, ThreadStartWithZeroInstructionPointer) {
  zx_handle_t thread;
  ASSERT_EQ(zx_thread_create(zx_process_self(), kThreadName, sizeof(kThreadName) - 1, 0, &thread),
            ZX_OK);

  test_exceptions::ExceptionCatcher catcher(*zx::unowned_process(zx_process_self()));
  ASSERT_EQ(zx_thread_start(thread, 0, 0, 0, 0), ZX_OK);

  auto result = catcher.ExpectException();
  ASSERT_TRUE(result.is_ok());
  ASSERT_OK(test_exceptions::ExitExceptionZxThread(std::move(result.value())));

  ASSERT_EQ(zx_handle_close(thread), ZX_OK);
}

TEST(Threads, NonstartedThread) {
  // Perform apis against non started threads (in the INITIAL STATE).
  zx_handle_t thread;

  ASSERT_EQ(zx_thread_create(zx_process_self(), "thread", 5, 0, &thread), ZX_OK);
  ASSERT_EQ(zx_task_kill(thread), ZX_OK);
  ASSERT_EQ(zx_task_kill(thread), ZX_OK);
  ASSERT_EQ(zx_handle_close(thread), ZX_OK);
}

TEST(Threads, InfoTaskStatsFails) {
  // Spin up a thread.
  zxr_thread_t thread;
  zx_handle_t thandle;
  ASSERT_TRUE(start_thread(threads_test_sleep_fn, (void*)zx_deadline_after(ZX_MSEC(100)), &thread,
                           &thandle));
  ASSERT_EQ(zx_object_wait_one(thandle, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL), ZX_OK);

  // Ensure that task_stats doesn't work on it.
  zx_info_task_stats_t info;
  EXPECT_NE(zx_object_get_info(thandle, ZX_INFO_TASK_STATS, &info, sizeof(info), NULL, NULL), ZX_OK,
            "Just added thread support to info_task_status?");
  // If so, replace this with a real test; see example in process.cpp.

  ASSERT_EQ(zx_handle_close(thandle), ZX_OK);
}

TEST(Threads, InfoThreadStatsFails) {
  // Spin up a thread.
  zxr_thread_t thread;
  zx_handle_t thandle;
  ASSERT_TRUE(start_thread(threads_test_sleep_fn, (void*)zx_deadline_after(ZX_MSEC(100)), &thread,
                           &thandle));
  ASSERT_EQ(zx_object_wait_one(thandle, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL), ZX_OK);

  // Ensure that thread_stats doesn't work on it.
  zx_info_task_stats_t info;
  EXPECT_EQ(zx_object_get_info(thandle, ZX_INFO_THREAD_STATS, &info, sizeof(info), NULL, NULL),
            ZX_ERR_BAD_STATE, "THREAD_STATS shouldn't work after a thread exits");
  ASSERT_EQ(zx_handle_close(thandle), ZX_OK);
}

TEST(Threads, GetLastScheduledCpu) {
  zx_handle_t event;
  ASSERT_EQ(zx_event_create(0, &event), ZX_OK);

  // Create a thread.
  zxr_thread_t thread;
  zx_handle_t thread_h;
  ThreadStarter starter;
  starter.CreateThread(&thread, &thread_h);

  // Ensure "last_cpu" is ZX_INFO_INVALID_CPU prior to the thread starting.
  zx_info_thread_stats_t info;
  ASSERT_EQ(
      zx_object_get_info(thread_h, ZX_INFO_THREAD_STATS, &info, sizeof(info), nullptr, nullptr),
      ZX_OK);
  ASSERT_EQ(info.last_scheduled_cpu, ZX_INFO_INVALID_CPU);

  // Start the thread.
  ASSERT_TRUE(starter.StartThread(threads_test_run_fn, &event));

  // Wait for worker to start.
  ASSERT_EQ(zx_object_wait_one(event, ZX_USER_SIGNAL_0, ZX_TIME_INFINITE, /*pending=*/nullptr),
            ZX_OK);

  // Ensure the last-reported thread looks reasonable.
  ASSERT_EQ(
      zx_object_get_info(thread_h, ZX_INFO_THREAD_STATS, &info, sizeof(info), nullptr, nullptr),
      ZX_OK);
  ASSERT_NE(info.last_scheduled_cpu, ZX_INFO_INVALID_CPU);
  ASSERT_LT(info.last_scheduled_cpu, ZX_CPU_SET_MAX_CPUS);

  // Shut down and clean up.
  ASSERT_EQ(zx_object_signal(event, 0, ZX_USER_SIGNAL_1), ZX_OK);
  ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, nullptr), ZX_OK);
  ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);
}

TEST(Threads, GetInfoRuntime) {
  zx::event event;
  ASSERT_EQ(zx::event::create(0, &event), ZX_OK);

  // Create a thread.
  zxr_thread_t thread;
  zx::thread thread_h;
  ThreadStarter starter;
  starter.CreateThread(&thread, thread_h.reset_and_get_address());

  // Ensure runtime is 0 prior to thread starting.
  zx_info_task_runtime_t info;
  ASSERT_EQ(thread_h.get_info(ZX_INFO_TASK_RUNTIME, &info, sizeof(info), nullptr, nullptr), ZX_OK);
  ASSERT_EQ(info.cpu_time, 0);
  ASSERT_EQ(info.queue_time, 0);

  // Start the thread.
  ASSERT_TRUE(starter.StartThread(threads_test_run_fn, &event));

  // Wait for worker to start.
  ASSERT_EQ(event.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), /*pending=*/nullptr), ZX_OK);

  // Ensure the last-reported thread looks reasonable.
  ASSERT_EQ(thread_h.get_info(ZX_INFO_TASK_RUNTIME, &info, sizeof(info), nullptr, nullptr), ZX_OK);
  ASSERT_GT(info.cpu_time, 0);
  ASSERT_GT(info.queue_time, 0);

  // Shut down and clean up.
  ASSERT_EQ(event.signal(0, ZX_USER_SIGNAL_1), ZX_OK);
  ASSERT_EQ(thread_h.wait_one(ZX_THREAD_TERMINATED, zx::time::infinite(), nullptr), ZX_OK);

  // Ensure the runtime can still be read after the task exits.
  ASSERT_EQ(thread_h.get_info(ZX_INFO_TASK_RUNTIME, &info, sizeof(info), nullptr, nullptr), ZX_OK);
  ASSERT_GT(info.cpu_time, 0);
  ASSERT_GT(info.queue_time, 0);

  // Test that removing ZX_RIGHT_INSPECT causes runtime calls to fail.
  zx_info_handle_basic_t basic;
  ASSERT_OK(thread_h.get_info(ZX_INFO_HANDLE_BASIC, &basic, sizeof(basic), nullptr, nullptr));
  zx::thread thread_dup;
  ASSERT_OK(thread_h.duplicate(basic.rights & ~ZX_RIGHT_INSPECT, &thread_dup));
  ASSERT_EQ(thread_dup.get_info(ZX_INFO_TASK_RUNTIME, &info, sizeof(info), nullptr, nullptr),
            ZX_ERR_ACCESS_DENIED);
}

TEST(Threads, GetAffinity) {
  // Create a thread.
  zxr_thread_t thread;
  zx_handle_t thread_h;
  ThreadStarter starter;
  starter.CreateThread(&thread, &thread_h);

  // Fetch affinity mask.
  zx_info_thread_t info;
  ASSERT_EQ(zx_object_get_info(thread_h, ZX_INFO_THREAD, &info, sizeof(info), nullptr, nullptr),
            ZX_OK);

  // We expect that a new thread should be runnable on at least 1 CPU.
  int num_cpus = 0;
  for (int i = 0; i < ZX_CPU_SET_MAX_CPUS; i++) {
    if (CpuMaskBitSet(info.cpu_affinity_mask, i)) {
      num_cpus++;
    }
  }
  ASSERT_GT(num_cpus, 0);

  // In the current system, we expect that a new thread will be runnable
  // on a contiguous range of CPUs, from 0 to (N - 1).
  for (int i = 0; i < ZX_CPU_SET_MAX_CPUS; i++) {
    EXPECT_EQ(CpuMaskBitSet(info.cpu_affinity_mask, i), i < num_cpus);
  }

  // Shut down and clean up.
  starter.DestroyThread();
  ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);
}

TEST(Threads, ResumeSuspended) {
  zx_handle_t event;
  zxr_thread_t thread;
  zx_handle_t thread_h;

  ASSERT_EQ(zx_event_create(0, &event), ZX_OK);
  ASSERT_TRUE(start_thread(threads_test_wait_fn, &event, &thread, &thread_h));

  // threads_test_wait_fn() uses zx_object_wait_one() so we watch for that.
  wait_thread_blocked(thread_h, ZX_THREAD_STATE_BLOCKED_WAIT_ONE);

  zx_handle_t suspend_token = ZX_HANDLE_INVALID;
  ASSERT_EQ(zx_task_suspend_token(thread_h, &suspend_token), ZX_OK);
  ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);

  // The thread should still be blocked on the event when it wakes up.
  // It needs to run for a bit to transition from suspended back to blocked
  // so we need to wait for it.
  wait_thread_blocked(thread_h, ZX_THREAD_STATE_BLOCKED_WAIT_ONE);

  // Check that signaling the event while suspended results in the expected behavior.
  suspend_token = ZX_HANDLE_INVALID;
  suspend_thread_synchronous(thread_h, &suspend_token);

  // Verify thread is suspended.
  zx_info_thread_t info;
  ASSERT_TRUE(get_thread_info(thread_h, &info));
  ASSERT_EQ(info.state, ZX_THREAD_STATE_SUSPENDED);
  ASSERT_EQ(info.wait_exception_channel_type, ZX_EXCEPTION_CHANNEL_TYPE_NONE);

  // Resuming the thread should mark the thread as blocked again.
  resume_thread_synchronous(thread_h, suspend_token);

  wait_thread_blocked(thread_h, ZX_THREAD_STATE_BLOCKED_WAIT_ONE);

  // When the thread is suspended the signaling should not take effect.
  suspend_token = ZX_HANDLE_INVALID;
  suspend_thread_synchronous(thread_h, &suspend_token);
  ASSERT_EQ(zx_object_signal(event, 0, ZX_USER_SIGNAL_0), ZX_OK);
  ASSERT_EQ(zx_object_wait_one(event, ZX_USER_SIGNAL_1, zx_deadline_after(ZX_MSEC(100)), NULL),
            ZX_ERR_TIMED_OUT);

  ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);

  ASSERT_EQ(zx_object_wait_one(event, ZX_USER_SIGNAL_1, ZX_TIME_INFINITE, NULL), ZX_OK);

  ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL), ZX_OK);

  ASSERT_EQ(zx_handle_close(event), ZX_OK);
  ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);
}

TEST(Threads, SuspendSleeping) {
  const zx_time_t sleep_deadline = zx_deadline_after(ZX_MSEC(100));
  zxr_thread_t thread;

  zx_handle_t thread_h;
  ASSERT_TRUE(start_thread(threads_test_sleep_fn, (void*)sleep_deadline, &thread, &thread_h));

  wait_thread_blocked(thread_h, ZX_THREAD_STATE_BLOCKED_SLEEPING);

  // Suspend the thread.
  zx_handle_t suspend_token = ZX_HANDLE_INVALID;
  zx_status_t status = zx_task_suspend_token(thread_h, &suspend_token);
  if (status != ZX_OK) {
    ASSERT_EQ(status, ZX_ERR_BAD_STATE);
    // This might happen if the thread exits before we tried suspending it
    // (due to e.g. a long context-switch away).  The system is too loaded
    // and so we might not have a chance at success here without a massive
    // sleep duration.
    zx_info_thread_t info;
    ASSERT_EQ(zx_object_get_info(thread_h, ZX_INFO_THREAD, &info, sizeof(info), NULL, NULL), ZX_OK);
    ASSERT_EQ(info.state, ZX_THREAD_STATE_DEAD);
    ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);
    // Early bail from the test, since we hit a possible race from an
    // overloaded machine.
    return;
  }
  ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_SUSPENDED, ZX_TIME_INFINITE, NULL), ZX_OK);
  ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);

  // Wait for the sleep to finish
  ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL), ZX_OK);

  const zx_time_t now = zx_clock_get_monotonic();
  ASSERT_GE(now, sleep_deadline, "thread did not sleep long enough");

  ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);
}

TEST(Threads, SuspendChannelCall) {
  zxr_thread_t thread;

  zx_handle_t channel;
  channel_call_suspend_test_arg thread_arg;
  ASSERT_EQ(zx_channel_create(0, &thread_arg.channel, &channel), ZX_OK);
  thread_arg.call_status = ZX_ERR_BAD_STATE;

  zx_handle_t thread_h;
  ASSERT_TRUE(start_thread(threads_test_channel_call_fn, &thread_arg, &thread, &thread_h));

  // Wait for the thread to send a channel call before suspending it
  ASSERT_EQ(zx_object_wait_one(channel, ZX_CHANNEL_READABLE, ZX_TIME_INFINITE, NULL), ZX_OK);

  // Suspend the thread.
  zx_handle_t suspend_token = ZX_HANDLE_INVALID;
  suspend_thread_synchronous(thread_h, &suspend_token);

  // Read the message
  uint8_t buf[9];
  uint32_t actual_bytes;
  ASSERT_EQ(zx_channel_read(channel, 0, buf, NULL, sizeof(buf), 0, &actual_bytes, NULL), ZX_OK);
  ASSERT_EQ(actual_bytes, sizeof(buf));
  ASSERT_EQ(memcmp(buf + sizeof(zx_txid_t), &"abcdefghi"[sizeof(zx_txid_t)],
                   sizeof(buf) - sizeof(zx_txid_t)),
            0);

  // Write a reply
  buf[8] = 'j';
  ASSERT_EQ(zx_channel_write(channel, 0, buf, sizeof(buf), NULL, 0), ZX_OK);

  // Make sure the remote channel didn't get signaled
  EXPECT_EQ(zx_object_wait_one(thread_arg.channel, ZX_CHANNEL_READABLE, 0, NULL), ZX_ERR_TIMED_OUT);

  // Make sure we can't read from the remote channel (the message should have
  // been reserved for the other thread, even though it is suspended).
  EXPECT_EQ(zx_channel_read(thread_arg.channel, 0, buf, NULL, sizeof(buf), 0, &actual_bytes, NULL),
            ZX_ERR_SHOULD_WAIT);

  // Wake the suspended thread
  ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);

  // Wait for the thread to finish
  ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL), ZX_OK);
  EXPECT_EQ(thread_arg.call_status, ZX_OK);

  ASSERT_EQ(zx_handle_close(channel), ZX_OK);
  ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);
}

TEST(Threads, SuspendPortCall) {
  zxr_thread_t thread;
  zx_handle_t port[2];
  ASSERT_EQ(zx_port_create(0, &port[0]), ZX_OK);
  ASSERT_EQ(zx_port_create(0, &port[1]), ZX_OK);

  zx_handle_t thread_h;
  ASSERT_TRUE(start_thread(threads_test_port_fn, port, &thread, &thread_h));

  wait_thread_blocked(thread_h, ZX_THREAD_STATE_BLOCKED_PORT);

  zx_handle_t suspend_token = ZX_HANDLE_INVALID;
  ASSERT_EQ(zx_task_suspend_token(thread_h, &suspend_token), ZX_OK);

  zx_port_packet_t packet1 = {100ull, ZX_PKT_TYPE_USER, 0u, {}};
  zx_port_packet_t packet2 = {300ull, ZX_PKT_TYPE_USER, 0u, {}};

  ASSERT_EQ(zx_port_queue(port[0], &packet1), ZX_OK);
  ASSERT_EQ(zx_port_queue(port[0], &packet2), ZX_OK);

  zx_port_packet_t packet;
  ASSERT_EQ(zx_port_wait(port[1], zx_deadline_after(ZX_MSEC(100)), &packet), ZX_ERR_TIMED_OUT);

  ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);

  ASSERT_EQ(zx_port_wait(port[1], ZX_TIME_INFINITE, &packet), ZX_OK);
  EXPECT_EQ(packet.key, 105ull);

  ASSERT_EQ(zx_port_wait(port[0], ZX_TIME_INFINITE, &packet), ZX_OK);
  EXPECT_EQ(packet.key, 300ull);

  ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL), ZX_OK);

  ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);
  ASSERT_EQ(zx_handle_close(port[0]), ZX_OK);
  ASSERT_EQ(zx_handle_close(port[1]), ZX_OK);
}

TEST(Threads, SuspendStopsThread) {
  zxr_thread_t thread;

  volatile int value = 0;
  zx_handle_t thread_h;
  ASSERT_TRUE(
      start_thread(threads_test_atomic_store, const_cast<int*>(&value), &thread, &thread_h));

  while (atomic_load(&value) != 1) {
    zx_nanosleep(0);
  }

  zx_handle_t suspend_token = ZX_HANDLE_INVALID;
  ASSERT_EQ(zx_task_suspend_token(thread_h, &suspend_token), ZX_OK);
  while (atomic_load(&value) != 2) {
    atomic_store(&value, 2);
    // Give the thread a chance to clobber the value
    zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));
  }
  ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);
  while (atomic_load(&value) != 1) {
    zx_nanosleep(0);
  }

  // Clean up.
  ASSERT_EQ(zx_task_kill(thread_h), ZX_OK);
  // Wait for the thread termination to complete.  We should do this so
  // that any later tests which handle process debug exceptions do not
  // receive an ZX_EXCP_THREAD_EXITING event.
  ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL), ZX_OK);
  ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);
}

TEST(Threads, SuspendMultiple) {
  zx_handle_t event;
  zxr_thread_t thread;
  zx_handle_t thread_h;

  ASSERT_EQ(zx_event_create(0, &event), ZX_OK);
  ASSERT_TRUE(start_thread(threads_test_wait_break_infinite_sleep_fn, &event, &thread, &thread_h));

  // The thread will now be blocked on the event. Wake it up and catch the trap (undefined
  // exception).
  zx_handle_t exception_channel, exception;
  ASSERT_EQ(zx_task_create_exception_channel(zx_process_self(), ZX_EXCEPTION_CHANNEL_DEBUGGER,
                                             &exception_channel),
            ZX_OK);
  ASSERT_EQ(zx_object_signal(event, 0, ZX_USER_SIGNAL_0), ZX_OK);
  wait_thread_excp_type(thread_h, exception_channel, ZX_EXCP_SW_BREAKPOINT, ZX_EXCP_THREAD_STARTING,
                        &exception);

  // The thread should now be blocked on a debugger exception.
  wait_thread_blocked(thread_h, ZX_THREAD_STATE_BLOCKED_EXCEPTION);
  zx_info_thread_t info;
  ASSERT_TRUE(get_thread_info(thread_h, &info));
  ASSERT_EQ(info.wait_exception_channel_type, ZX_EXCEPTION_CHANNEL_TYPE_DEBUGGER);

  advance_over_breakpoint(thread_h);

  // Suspend twice (on top of the existing exception). Don't use the synchronous suspend since
  // suspends don't escape out of exception handling, unlike blocking
  // syscalls where suspend will escape out of them.
  zx_handle_t suspend_token1 = ZX_HANDLE_INVALID;
  ASSERT_EQ(zx_task_suspend_token(thread_h, &suspend_token1), ZX_OK);
  zx_handle_t suspend_token2 = ZX_HANDLE_INVALID;
  ASSERT_EQ(zx_task_suspend_token(thread_h, &suspend_token2), ZX_OK);

  // Resume one token, it should remain blocked.
  ASSERT_EQ(zx_handle_close(suspend_token1), ZX_OK);
  ASSERT_TRUE(get_thread_info(thread_h, &info));
  // Note: If this check is flaky, it's failing. It should not transition out of the blocked
  // state, but if it does so, it will do so asynchronously which might cause
  // nondeterministic failures.
  ASSERT_EQ(info.state, ZX_THREAD_STATE_BLOCKED_EXCEPTION);

  // Resume the exception. It should be SUSPENDED now that the exception is complete (one could
  // argue that it could still be BLOCKED also, but it's not in the current implementation).
  // The transition to SUSPENDED happens asynchronously unlike some of the exception states.
  uint32_t state = ZX_EXCEPTION_STATE_HANDLED;
  ASSERT_EQ(zx_object_set_property(exception, ZX_PROP_EXCEPTION_STATE, &state, sizeof(state)),
            ZX_OK);
  ASSERT_EQ(zx_handle_close(exception), ZX_OK);
  zx_signals_t observed = 0u;
  ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_SUSPENDED, ZX_TIME_INFINITE, &observed), ZX_OK);

  ASSERT_TRUE(get_thread_info(thread_h, &info));
  ASSERT_EQ(info.state, ZX_THREAD_STATE_SUSPENDED);

  // 2nd resume, should be running or sleeping after this.
  resume_thread_synchronous(thread_h, suspend_token2);
  ASSERT_TRUE(get_thread_info(thread_h, &info));
  ASSERT_TRUE(info.state == ZX_THREAD_STATE_RUNNING ||
              info.state == ZX_THREAD_STATE_BLOCKED_SLEEPING);

  // Clean up.
  ASSERT_EQ(zx_task_kill(thread_h), ZX_OK);
  EXPECT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, nullptr), ZX_OK);
  ASSERT_EQ(zx_handle_close(exception_channel), ZX_OK);
  ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);
}

TEST(Threads, SuspendSelf) {
  zx_handle_t suspend_token;
  EXPECT_EQ(zx_task_suspend(zx_thread_self(), &suspend_token), ZX_ERR_NOT_SUPPORTED);
}

TEST(Threads, SuspendAfterDeath) {
  zxr_thread_t thread;
  zx_handle_t thread_h;
  ASSERT_TRUE(start_thread(threads_test_infinite_sleep_fn, nullptr, &thread, &thread_h));
  ASSERT_EQ(zx_task_kill(thread_h), ZX_OK);

  zx_handle_t suspend_token = ZX_HANDLE_INVALID;
  EXPECT_EQ(zx_task_suspend(thread_h, &suspend_token), ZX_ERR_BAD_STATE);
  EXPECT_EQ(suspend_token, ZX_HANDLE_INVALID);
  EXPECT_EQ(zx_handle_close(suspend_token), ZX_OK);

  EXPECT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, nullptr), ZX_OK);
  EXPECT_EQ(zx_handle_close(thread_h), ZX_OK);
}

// This tests for a bug in which killing a suspended thread causes the
// thread to be resumed and execute more instructions in userland.
TEST(Threads, KillSuspendedThread) {
  std::atomic<int> value = 0;
  zxr_thread_t thread;
  zx_handle_t thread_h;
  ASSERT_TRUE(start_thread(threads_test_atomic_store, &value, &thread, &thread_h));

  // Wait until the thread has started and has modified value.
  while (value != 1) {
    zx_nanosleep(0);
  }

  zx_handle_t suspend_token = ZX_HANDLE_INVALID;
  suspend_thread_synchronous(thread_h, &suspend_token);

  // Attach to debugger channel so we can see ZX_EXCP_THREAD_EXITING.
  zx_handle_t exception_channel;
  ASSERT_EQ(zx_task_create_exception_channel(zx_process_self(), ZX_EXCEPTION_CHANNEL_DEBUGGER,
                                             &exception_channel),
            ZX_OK);

  // Reset the test memory location.
  value = 100;
  ASSERT_EQ(zx_task_kill(thread_h), ZX_OK);
  // Wait for the thread termination to complete.
  ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL), ZX_OK);
  // Check for the bug.  The thread should not have resumed execution and
  // so should not have modified value.
  EXPECT_EQ(value.load(), 100);

  // Check that the thread is reported as exiting and not as resumed.
  zx_handle_t exception;
  wait_thread_excp_type(thread_h, exception_channel, ZX_EXCP_THREAD_EXITING, 0, &exception);

  // Clean up.
  ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);
  ASSERT_EQ(zx_handle_close(exception), ZX_OK);
  ASSERT_EQ(zx_handle_close(exception_channel), ZX_OK);
  ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);
}

// Suspend a thread before starting and make sure it starts into suspended state.
TEST(Threads, StartSuspendedThread) {
  zxr_thread_t thread;
  zx_handle_t thread_h;
  ThreadStarter starter;
  starter.CreateThread(&thread, &thread_h, true);

  // Suspend first, then start the thread.
  zx_handle_t suspend_token = ZX_HANDLE_INVALID;
  ASSERT_EQ(zx_task_suspend(thread_h, &suspend_token), ZX_OK);

  std::atomic<int> value = 0;
  ASSERT_TRUE(starter.StartThread(threads_test_atomic_store, &value));

  // Make sure the thread goes directly to suspended state without executing at all.
  ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_SUSPENDED, ZX_TIME_INFINITE, NULL), ZX_OK);

  // Once we know it's suspended, give it a real stack.
  starter.GrowStackVmo();

  // Make sure the thread still resumes properly.
  ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);
  ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_RUNNING, ZX_TIME_INFINITE, NULL), ZX_OK);
  while (value != 1) {
    zx_nanosleep(0);
  }

  // Clean up.
  ASSERT_EQ(zx_task_kill(thread_h), ZX_OK);
  ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL), ZX_OK);
  ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);
}

// Suspend and resume a thread before starting, it should start as normal.
TEST(Threads, StartSuspendedAndResumedThread) {
  zxr_thread_t thread;
  zx_handle_t thread_h;
  ThreadStarter starter;
  starter.CreateThread(&thread, &thread_h);

  // Suspend and resume.
  zx_handle_t suspend_token = ZX_HANDLE_INVALID;
  ASSERT_EQ(zx_task_suspend(thread_h, &suspend_token), ZX_OK);
  ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);

  // Start the thread, it should behave normally.
  std::atomic<int> value = 0;
  ASSERT_TRUE(starter.StartThread(threads_test_atomic_store, &value));
  ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_RUNNING, ZX_TIME_INFINITE, NULL), ZX_OK);
  while (value != 1) {
    zx_nanosleep(0);
  }

  // Clean up.
  ASSERT_EQ(zx_task_kill(thread_h), ZX_OK);
  ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL), ZX_OK);
  ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);
}

static void port_wait_for_signal(zx_handle_t port, zx_handle_t thread, zx_time_t deadline,
                                 zx_signals_t mask, zx_port_packet_t* packet) {
  ASSERT_EQ(zx_object_wait_async(thread, port, 0u, mask, 0), ZX_OK);
  ASSERT_EQ(zx_port_wait(port, deadline, packet), ZX_OK);
  ASSERT_EQ(packet->type, ZX_PKT_TYPE_SIGNAL_ONE);
}

// Test signal delivery of suspended threads via async wait.
static void TestSuspendWaitAsyncSignalDeliveryWorker() {
  zx_handle_t event;
  zx_handle_t port;
  zxr_thread_t thread;
  zx_handle_t thread_h;
  const zx_signals_t run_susp_mask = ZX_THREAD_RUNNING | ZX_THREAD_SUSPENDED;

  ASSERT_EQ(zx_event_create(0, &event), ZX_OK);
  ASSERT_TRUE(start_thread(threads_test_wait_fn, &event, &thread, &thread_h));

  ASSERT_EQ(zx_port_create(0, &port), ZX_OK);

  zx_port_packet_t packet;
  // There should be a RUNNING signal packet present and not SUSPENDED.
  // This is from when the thread first started to run.
  port_wait_for_signal(port, thread_h, 0u, run_susp_mask, &packet);
  ASSERT_EQ(packet.signal.observed & run_susp_mask, ZX_THREAD_RUNNING);

  // Make sure there are no more packets.
  // RUNNING or SUSPENDED is always asserted.
  ASSERT_EQ(zx_object_wait_async(thread_h, port, 0u, ZX_THREAD_SUSPENDED, 0), ZX_OK);
  ASSERT_EQ(zx_port_wait(port, 0u, &packet), ZX_ERR_TIMED_OUT);
  ASSERT_EQ(zx_port_cancel(port, thread_h, 0u), ZX_OK);

  zx_handle_t suspend_token = ZX_HANDLE_INVALID;
  suspend_thread_synchronous(thread_h, &suspend_token);

  zx_info_thread_t info;
  ASSERT_TRUE(get_thread_info(thread_h, &info));
  ASSERT_EQ(info.state, ZX_THREAD_STATE_SUSPENDED);

  resume_thread_synchronous(thread_h, suspend_token);
  ASSERT_TRUE(get_thread_info(thread_h, &info));
  // At this point the thread may be running or blocked waiting for an
  // event. Either one is fine. threads_test_wait_fn() uses
  // zx_object_wait_one() so we watch for that.
  ASSERT_TRUE(info.state == ZX_THREAD_STATE_RUNNING ||
              info.state == ZX_THREAD_STATE_BLOCKED_WAIT_ONE);

  // We should see just RUNNING,
  // and it should be immediately present (no deadline).
  port_wait_for_signal(port, thread_h, 0u, run_susp_mask, &packet);
  ASSERT_EQ(packet.signal.observed & run_susp_mask, ZX_THREAD_RUNNING);

  // The thread should still be blocked on the event when it wakes up.
  wait_thread_blocked(thread_h, ZX_THREAD_STATE_BLOCKED_WAIT_ONE);

  // Check that suspend/resume while blocked in a syscall results in
  // the expected behavior and is visible via async wait.
  suspend_token = ZX_HANDLE_INVALID;
  ASSERT_EQ(zx_task_suspend_token(thread_h, &suspend_token), ZX_OK);
  port_wait_for_signal(port, thread_h, zx_deadline_after(ZX_MSEC(100)), ZX_THREAD_SUSPENDED,
                       &packet);
  ASSERT_EQ(packet.signal.observed & run_susp_mask, ZX_THREAD_SUSPENDED);

  ASSERT_TRUE(get_thread_info(thread_h, &info));
  ASSERT_EQ(info.state, ZX_THREAD_STATE_SUSPENDED);
  ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);
  port_wait_for_signal(port, thread_h, zx_deadline_after(ZX_MSEC(100)), ZX_THREAD_RUNNING, &packet);
  ASSERT_EQ(packet.signal.observed & run_susp_mask, ZX_THREAD_RUNNING);

  // Resumption from being suspended back into a blocking syscall will be
  // in the RUNNING state and then BLOCKED.
  wait_thread_blocked(thread_h, ZX_THREAD_STATE_BLOCKED_WAIT_ONE);

  ASSERT_EQ(zx_object_signal(event, 0, ZX_USER_SIGNAL_0), ZX_OK);
  ASSERT_EQ(zx_object_wait_one(event, ZX_USER_SIGNAL_1, ZX_TIME_INFINITE, NULL), ZX_OK);

  ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL), ZX_OK);

  ASSERT_EQ(zx_handle_close(port), ZX_OK);
  ASSERT_EQ(zx_handle_close(event), ZX_OK);
  ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);
}

// Test signal delivery of suspended threads via single async wait.
TEST(Threads, SuspendSingleWaitAsyncSignalDelivery) { TestSuspendWaitAsyncSignalDeliveryWorker(); }

// Test signal delivery of suspended threads via repeating async wait.
TEST(Threads, SuspendRepeatingWaitAsyncSignalDelivery) {
  TestSuspendWaitAsyncSignalDeliveryWorker();
}

// Helper class for setting up a test for reading register state from a worker thread.
template <typename RegisterStruct>
class RegisterReadSetup {
 public:
  using ThreadFunc = void (*)(RegisterStruct*);

  RegisterReadSetup() = default;
  ~RegisterReadSetup() {
    zx_handle_close(suspend_token_);
    zx_task_kill(thread_handle_);
    // Wait for the thread termination to complete.
    zx_object_wait_one(thread_handle_, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL);
    zx_handle_close(thread_handle_);
  }

  zx_handle_t thread_handle() const { return thread_handle_; }

  // Run |thread_func| with |state|.  Once the thread reaches |expected_pc|, return, leaving the
  // thread suspended.
  void RunUntil(ThreadFunc thread_func, RegisterStruct* state, uintptr_t expected_pc) {
    ASSERT_TRUE(start_thread((void (*)(void*))thread_func, state, &thread_, &thread_handle_));

    while (true) {
      ASSERT_EQ(zx_nanosleep(zx_deadline_after(ZX_MSEC(1))), ZX_OK);
      Suspend();
      zx_thread_state_general_regs_t regs;
      ASSERT_EQ(
          zx_thread_read_state(thread_handle_, ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)),
          ZX_OK);
      if (regs.REG_PC == expected_pc) {
        break;
      }
      Resume();
    }
  }

  void Resume() { resume_thread_synchronous(thread_handle_, suspend_token_); }

  void Suspend() { suspend_thread_synchronous(thread_handle_, &suspend_token_); }

 private:
  zxr_thread_t thread_;
  zx_handle_t thread_handle_ = ZX_HANDLE_INVALID;
  zx_handle_t suspend_token_ = ZX_HANDLE_INVALID;
};

// This tests the registers reported by zx_thread_read_state() for a
// suspended thread.  It starts a thread which sets all the registers to
// known test values.
TEST(Threads, ReadingGeneralRegisterState) {
  zx_thread_state_general_regs_t gen_regs_expected;
  general_regs_fill_test_values(&gen_regs_expected);
  gen_regs_expected.REG_PC = (uintptr_t)spin_address;

  RegisterReadSetup<zx_thread_state_general_regs_t> setup;
  setup.RunUntil(&spin_with_general_regs, &gen_regs_expected,
                 reinterpret_cast<uintptr_t>(&spin_address));

  zx_thread_state_general_regs_t regs;
  ASSERT_EQ(zx_thread_read_state(setup.thread_handle(), ZX_THREAD_STATE_GENERAL_REGS, &regs,
                                 sizeof(regs)),
            ZX_OK);
  ASSERT_NO_FATAL_FAILURES(general_regs_expect_eq(regs, gen_regs_expected));
}

TEST(Threads, ReadingFpRegisterState) {
  zx_thread_state_fp_regs_t fp_regs_expected;
  fp_regs_fill_test_values(&fp_regs_expected);

  RegisterReadSetup<zx_thread_state_fp_regs_t> setup;
  setup.RunUntil(&spin_with_fp_regs, &fp_regs_expected, reinterpret_cast<uintptr_t>(&spin_address));

  zx_thread_state_fp_regs_t regs;
  zx_status_t status =
      zx_thread_read_state(setup.thread_handle(), ZX_THREAD_STATE_FP_REGS, &regs, sizeof(regs));
#if defined(__x86_64__)
  ASSERT_EQ(status, ZX_OK);
  ASSERT_NO_FATAL_FAILURES(fp_regs_expect_eq(regs, fp_regs_expected));
#elif defined(__aarch64__)
  ASSERT_EQ(status, ZX_ERR_NOT_SUPPORTED);
#else
#error unsupported platform
#endif
}

TEST(Threads, ReadingVectorRegisterState) {
  zx_thread_state_vector_regs_t vector_regs_expected;
  vector_regs_fill_test_values(&vector_regs_expected);

  RegisterReadSetup<zx_thread_state_vector_regs_t> setup;
  setup.RunUntil(&spin_with_vector_regs, &vector_regs_expected,
                 reinterpret_cast<uintptr_t>(&spin_address));

  zx_thread_state_vector_regs_t regs;
  memset(&regs, 0xff, sizeof(regs));
  ASSERT_EQ(
      zx_thread_read_state(setup.thread_handle(), ZX_THREAD_STATE_VECTOR_REGS, &regs, sizeof(regs)),
      ZX_OK);

  ASSERT_NO_FATAL_FAILURES(vector_regs_expect_unsupported_are_zero(regs));
  ASSERT_NO_FATAL_FAILURES(vector_regs_expect_eq(regs, vector_regs_expected));
}

// Procedure:
//  1. Call Init() which will start a thread and suspend it.
//  2. Write the register state you want to the thread_handle().
//  3. Call DoSave with the save function and pointer. This will execute that code in the context of
//     the thread.
template <typename RegisterStruct>
class RegisterWriteSetup {
 public:
  using SaveFunc = void (*)();

  RegisterWriteSetup() = default;
  ~RegisterWriteSetup() { zx_handle_close(thread_handle_); }

  zx_handle_t thread_handle() const { return thread_handle_; }

  void Init() {
    ASSERT_TRUE(start_thread(threads_test_atomic_store, &p_, &thread_, &thread_handle_));

    // Wait for the thread to begin executing.
    while (p_.load() == 0) {
      zx_nanosleep(zx_deadline_after(THREAD_BLOCKED_WAIT_DURATION));
    }

    suspend_thread_synchronous(thread_handle_, &suspend_token_);
  }

  // The IP and SP set in the general registers will be filled in to the optional output
  // parameters. This is for the general register test since we change those values out from
  // under it.
  void DoSave(SaveFunc save_func, RegisterStruct* out, uint64_t* general_ip = nullptr,
              uint64_t* general_sp = nullptr) {
    // Modify the PC to point to the routine, and the SP to point to the output struct.
    zx_thread_state_general_regs_t general_regs;
    ASSERT_EQ(zx_thread_read_state(thread_handle_, ZX_THREAD_STATE_GENERAL_REGS, &general_regs,
                                   sizeof(general_regs)),
              ZX_OK);

    struct {
      // A small stack that is used for calling zx_thread_exit().
      char stack[1024] __ALIGNED(16);
      RegisterStruct regs_got;  // STACK_PTR will point here.
    } stack;
    general_regs.REG_PC = (uintptr_t)save_func;
    general_regs.REG_STACK_PTR = (uintptr_t)(stack.stack + sizeof(stack.stack));
    ASSERT_EQ(zx_thread_write_state(thread_handle_, ZX_THREAD_STATE_GENERAL_REGS, &general_regs,
                                    sizeof(general_regs)),
              ZX_OK);

    if (general_ip)
      *general_ip = general_regs.REG_PC;
    if (general_sp)
      *general_sp = general_regs.REG_STACK_PTR;

    // Unsuspend the thread and wait for it to finish executing, this will run the code
    // and fill the RegisterStruct we passed.
    ASSERT_EQ(zx_handle_close(suspend_token_), ZX_OK);
    suspend_token_ = ZX_HANDLE_INVALID;
    ASSERT_EQ(zx_object_wait_one(thread_handle_, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL),
              ZX_OK);

    memcpy(out, &stack.regs_got, sizeof(RegisterStruct));
  }

 private:
  std::atomic<int> p_ = 0;
  zxr_thread_t thread_;
  zx_handle_t thread_handle_ = ZX_HANDLE_INVALID;
  zx_handle_t suspend_token_ = ZX_HANDLE_INVALID;
};

// This tests writing registers using zx_thread_write_state().  After
// setting registers using that syscall, it reads back the registers and
// checks their values.
TEST(Threads, WritingGeneralRegisterState) {
  RegisterWriteSetup<zx_thread_state_general_regs_t> setup;
  setup.Init();

  // Set the general registers.
  zx_thread_state_general_regs_t regs_to_set;
  general_regs_fill_test_values(&regs_to_set);
  ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_GENERAL_REGS, &regs_to_set,
                                  sizeof(regs_to_set)),
            ZX_OK);

  zx_thread_state_general_regs_t regs;
  uint64_t ip = 0, sp = 0;
  setup.DoSave(&save_general_regs_and_exit_thread, &regs, &ip, &sp);

  // Fix up the expected values with the IP/SP required for the register read.
  regs_to_set.REG_PC = ip;
  regs_to_set.REG_STACK_PTR = sp;
  ASSERT_NO_FATAL_FAILURES(general_regs_expect_eq(regs_to_set, regs));
}

// This tests writing single step state using zx_thread_write_state().
TEST(Threads, WritingSingleStepState) {
  RegisterWriteSetup<zx_thread_state_single_step_t> setup;
  setup.Init();

  // 0 is valid.
  zx_thread_state_single_step_t single_step = 0;
  ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_SINGLE_STEP, &single_step,
                                  sizeof(single_step)),
            ZX_OK);

  // 1 is valid.
  single_step = 1;
  ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_SINGLE_STEP, &single_step,
                                  sizeof(single_step)),
            ZX_OK);

  // All other values are invalid.
  single_step = 2;
  ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_SINGLE_STEP, &single_step,
                                  sizeof(single_step)),
            ZX_ERR_INVALID_ARGS);
  single_step = UINT32_MAX;
  ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_SINGLE_STEP, &single_step,
                                  sizeof(single_step)),
            ZX_ERR_INVALID_ARGS);

  // Buffer can be larger than necessary.
  single_step = 0;
  ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_SINGLE_STEP, &single_step,
                                  sizeof(single_step) + 1),
            ZX_OK);
  // But not smaller.
  ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_SINGLE_STEP, &single_step,
                                  sizeof(single_step) - 1),
            ZX_ERR_BUFFER_TOO_SMALL);
}

TEST(Threads, WritingFpRegisterState) {
  RegisterWriteSetup<zx_thread_state_fp_regs_t> setup;
  setup.Init();

  // The busyloop code executed initially by the setup class will have executed an MMX instruction
  // so that the MMX state is available to write.
  zx_thread_state_fp_regs_t regs_to_set;
  fp_regs_fill_test_values(&regs_to_set);

  zx_status_t status = zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_FP_REGS,
                                             &regs_to_set, sizeof(regs_to_set));

#if defined(__x86_64__)
  ASSERT_EQ(status, ZX_OK);

  zx_thread_state_fp_regs_t regs;
  setup.DoSave(&save_fp_regs_and_exit_thread, &regs);
  ASSERT_NO_FATAL_FAILURES(fp_regs_expect_eq(regs_to_set, regs));
#elif defined(__aarch64__)
  ASSERT_EQ(status, ZX_ERR_NOT_SUPPORTED);
#else
#error unsupported platform
#endif
}

TEST(Threads, WritingVectorRegisterState) {
  RegisterWriteSetup<zx_thread_state_vector_regs_t> setup;
  setup.Init();

  zx_thread_state_vector_regs_t regs_to_set;
  vector_regs_fill_test_values(&regs_to_set);
  ASSERT_NO_FATAL_FAILURES(vector_regs_expect_unsupported_are_zero(regs_to_set));

  ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_VECTOR_REGS, &regs_to_set,
                                  sizeof(regs_to_set)),
            ZX_OK);

  zx_thread_state_vector_regs_t regs;
  setup.DoSave(&save_vector_regs_and_exit_thread, &regs);
  ASSERT_NO_FATAL_FAILURES(vector_regs_expect_eq(regs_to_set, regs));
}

TEST(Threads, WritingVectorRegisterState_UnsupportedFieldsIgnored) {
  RegisterWriteSetup<zx_thread_state_vector_regs_t> setup;
  setup.Init();

  zx_thread_state_vector_regs_t regs;
  vector_regs_fill_test_values(&regs);

#if defined(__x86_64__)
  // Fill in the fields corresponding to unsupported features so we can later verify they are zeroed
  // out by |zx_thread_read_state|.
  for (int reg = 0; reg < 16; reg++) {
    for (int i = 5; i < 8; i++) {
      regs.zmm[reg].v[i] = 0xfffffffffffffffful;
    }
  }
  for (int reg = 16; reg < 32; reg++) {
    for (int i = 0; i < 8; i++) {
      regs.zmm[reg].v[i] = 0xfffffffffffffffful;
    }
  }
#endif

  ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_VECTOR_REGS, &regs,
                                  sizeof(regs)),
            ZX_OK);
  ASSERT_EQ(
      zx_thread_read_state(setup.thread_handle(), ZX_THREAD_STATE_VECTOR_REGS, &regs, sizeof(regs)),
      ZX_OK);

  ASSERT_NO_FATAL_FAILURES(vector_regs_expect_unsupported_are_zero(regs));

  zx_thread_state_vector_regs_t vector_regs_expected;
  vector_regs_fill_test_values(&vector_regs_expected);
  ASSERT_NO_FATAL_FAILURES(vector_regs_expect_eq(regs, vector_regs_expected));
}

// Test for fxbug.dev/50632: Make sure zx_thread_write_state doesn't overwrite
// reserved bits in mxcsr (x64 only).
TEST(Threads, WriteThreadStateWithInvalidMxcsrIsInvalidArgs) {
#if defined(__x86_64__)
  RegisterWriteSetup<zx_thread_state_vector_regs_t> setup;
  setup.Init();

  zx_thread_state_vector_regs_t start_values;
  ASSERT_OK(zx_thread_read_state(setup.thread_handle(), ZX_THREAD_STATE_VECTOR_REGS, &start_values,
                                 sizeof(start_values)));

  zx_thread_state_vector_regs_t regs_to_set;
  vector_regs_fill_test_values(&regs_to_set);
  regs_to_set.mxcsr = 0xffffffff;

  EXPECT_EQ(ZX_ERR_INVALID_ARGS,
            zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_VECTOR_REGS, &regs_to_set,
                                  sizeof(regs_to_set)));

  zx_thread_state_vector_regs_t end_values;
  setup.DoSave(&save_vector_regs_and_exit_thread, &end_values);
  ASSERT_NO_FATAL_FAILURES(vector_regs_expect_eq(start_values, end_values));
#endif  // defined(__x86_64__)
}

// This test starts a thread which reads and writes from TLS.
TEST(Threads, ThreadLocalRegisterState) {
  RegisterWriteSetup<struct thread_local_regs> setup;
  setup.Init();

  zx_thread_state_general_regs_t regs = {};

#if defined(__x86_64__)
  // The thread will read these from the fs and gs base addresses
  // into the output regs struct, and then write different numbers.
  uint64_t fs_base_value = 0x1234;
  uint64_t gs_base_value = 0x5678;
  regs.fs_base = (uintptr_t)&fs_base_value;
  regs.gs_base = (uintptr_t)&gs_base_value;
#elif defined(__aarch64__)
  uint64_t tpidr_value = 0x1234;
  regs.tpidr = (uintptr_t)&tpidr_value;
#endif

  ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_GENERAL_REGS, &regs,
                                  sizeof(regs)),
            ZX_OK);

  struct thread_local_regs tls_regs;
  setup.DoSave(&save_thread_local_regs_and_exit_thread, &tls_regs);

#if defined(__x86_64__)
  EXPECT_EQ(tls_regs.fs_base_value, 0x1234);
  EXPECT_EQ(tls_regs.gs_base_value, 0x5678);
  EXPECT_EQ(fs_base_value, 0x12345678);
  EXPECT_EQ(gs_base_value, 0x7890abcd);
#elif defined(__aarch64__)
  EXPECT_EQ(tls_regs.tpidr_value, 0x1234);
  EXPECT_EQ(tpidr_value, 0x12345678);
#endif
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

TEST(Threads, ThreadStartInvalidEntry) {
  auto test_thread_start = [&](uintptr_t pc, zx_status_t expected) {
    zx_handle_t process = zx_process_self();
    zx_handle_t thread = ZX_HANDLE_INVALID;
    ASSERT_OK(
        zx_thread_create(process, kThreadName, sizeof(kThreadName) - 1, 0 /* options */, &thread));
    char stack[1024] __ALIGNED(16);  // a small stack for the thread.
    uintptr_t thread_stack = reinterpret_cast<uintptr_t>(&stack[1024]);

    EXPECT_EQ(expected, zx_thread_start(thread, pc, thread_stack, 0 /* arg0 */, 0 /* arg1 */));
    zx_handle_close(thread);
  };

  uintptr_t non_user_pc = 0x1UL;
  uintptr_t kernel_pc = 0xffffff8000000000UL;

  test_thread_start(non_user_pc, ZX_ERR_INVALID_ARGS);
  test_thread_start(kernel_pc, ZX_ERR_INVALID_ARGS);

#if defined(__x86_64__)
  uintptr_t non_canonical_pc = ((uintptr_t)1) << (x86_linear_address_width() - 1);
  test_thread_start(non_canonical_pc, ZX_ERR_INVALID_ARGS);
#endif  // defined(__x86_64__)
}

// Test that zx_thread_write_state() does not allow setting RIP to a
// non-canonical address for a thread that was suspended inside a syscall,
// because if the kernel returns to that address using SYSRET, that can
// cause a fault in kernel mode that is exploitable.  See
// sysret_problem.md.
TEST(Threads, NoncanonicalRipAddressSyscall) {
#if defined(__x86_64__)
  zx_handle_t event;
  ASSERT_EQ(zx_event_create(0, &event), ZX_OK);
  zxr_thread_t thread;
  zx_handle_t thread_handle;
  ASSERT_TRUE(start_thread(threads_test_wait_fn, &event, &thread, &thread_handle));

  // Wait until the thread has entered the syscall.
  wait_thread_blocked(thread_handle, ZX_THREAD_STATE_BLOCKED_WAIT_ONE);

  zx_handle_t suspend_token = ZX_HANDLE_INVALID;
  suspend_thread_synchronous(thread_handle, &suspend_token);

  zx_thread_state_general_regs_t regs;
  ASSERT_EQ(zx_thread_read_state(thread_handle, ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)),
            ZX_OK);

  // Example addresses to test.
  uintptr_t noncanonical_addr = ((uintptr_t)1) << (x86_linear_address_width() - 1);
  uintptr_t canonical_addr = noncanonical_addr - 1;
  uint64_t kKernelAddr = 0xffffff8000000000UL;

  zx_thread_state_general_regs_t regs_modified = regs;

  // This RIP address must be disallowed.
  regs_modified.rip = noncanonical_addr;
  ASSERT_EQ(zx_thread_write_state(thread_handle, ZX_THREAD_STATE_GENERAL_REGS, &regs_modified,
                                  sizeof(regs_modified)),
            ZX_ERR_INVALID_ARGS);

  regs_modified.rip = canonical_addr;
  ASSERT_EQ(zx_thread_write_state(thread_handle, ZX_THREAD_STATE_GENERAL_REGS, &regs_modified,
                                  sizeof(regs_modified)),
            ZX_OK);

  // This RIP address does not need to be disallowed, but it is currently
  // disallowed because this simplifies the check and it's not useful to
  // allow this address.
  regs_modified.rip = kKernelAddr;
  ASSERT_EQ(zx_thread_write_state(thread_handle, ZX_THREAD_STATE_GENERAL_REGS, &regs_modified,
                                  sizeof(regs_modified)),
            ZX_ERR_INVALID_ARGS);

  // Clean up: Restore the original register state.
  ASSERT_EQ(zx_thread_write_state(thread_handle, ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)),
            ZX_OK);
  // Allow the child thread to resume and exit.
  ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);
  ASSERT_EQ(zx_object_signal(event, 0, ZX_USER_SIGNAL_0), ZX_OK);
  // Wait for the child thread to signal that it has continued.
  ASSERT_EQ(zx_object_wait_one(event, ZX_USER_SIGNAL_1, ZX_TIME_INFINITE, NULL), ZX_OK);
  // Wait for the child thread to exit.
  ASSERT_EQ(zx_object_wait_one(thread_handle, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL), ZX_OK);
  ASSERT_EQ(zx_handle_close(event), ZX_OK);
  ASSERT_EQ(zx_handle_close(thread_handle), ZX_OK);
#endif
}

// Test that zx_thread_write_state() does not allow setting RIP to a
// non-canonical address for a thread that was suspended inside an interrupt,
// because if the kernel returns to that address using IRET, that can
// cause a fault in kernel mode that is exploitable.
// See docs/concepts/kernel/sysret_problem.md
TEST(Threads, NoncanonicalRipAddressIRETQ) {
#if defined(__x86_64__)
  // Example addresses to test.
  uintptr_t noncanonical_addr = ((uintptr_t)1) << (x86_linear_address_width() - 1);
  uintptr_t kernel_addr = 0xffffff8000000000UL;

  // canonical address that is safe to resume the thread to.
  uintptr_t canonical_addr = reinterpret_cast<uintptr_t>(&spin_address);

  auto test_rip_value = [&](uintptr_t address, zx_status_t expected) {
    zx_thread_state_general_regs_t func_regs;
    RegisterReadSetup<zx_thread_state_general_regs_t> setup;
    setup.RunUntil(&spin_with_general_regs, &func_regs, reinterpret_cast<uintptr_t>(&spin_address));

    zx_thread_state_general_regs_t regs;
    ASSERT_OK(zx_thread_read_state(setup.thread_handle(), ZX_THREAD_STATE_GENERAL_REGS, &regs,
                                   sizeof(regs)));

    regs.rip = address;
    EXPECT_EQ(expected, zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_GENERAL_REGS,
                                              &regs, sizeof(regs)));

    // Resume and re-suspend the thread. Even if the zx_thread_write_state
    // returns an error but sets the registers, we still want to observe the
    // crash. Note that there is no guarantee that it would happen, as the
    // thread might get supsended before it even resumes execution.
    setup.Resume();
    setup.Suspend();
  };

  test_rip_value(canonical_addr, ZX_OK);

  test_rip_value(noncanonical_addr, ZX_ERR_INVALID_ARGS);
  test_rip_value(kernel_addr, ZX_ERR_INVALID_ARGS);

#endif  // defined(__x86_64__)
}

// Test that, on ARM64, userland cannot use zx_thread_write_state() to
// modify flag bits such as I and F (bits 7 and 6), which are the IRQ and
// FIQ interrupt disable flags.  We don't want userland to be able to set
// those flags to 1, since that would disable interrupts.  Also, userland
// should not be able to read these bits.
TEST(Threads, WritingArmFlagsRegister) {
#if defined(__aarch64__)
  std::atomic<int> value = 0;
  zxr_thread_t thread;
  zx_handle_t thread_handle;
  ASSERT_TRUE(start_thread(threads_test_atomic_store, &value, &thread, &thread_handle));
  // Wait for the thread to start executing and enter its main loop.
  while (value != 1) {
    ASSERT_EQ(zx_nanosleep(zx_deadline_after(ZX_USEC(1))), ZX_OK);
  }
  zx_handle_t suspend_token = ZX_HANDLE_INVALID;
  suspend_thread_synchronous(thread_handle, &suspend_token);

  zx_thread_state_general_regs_t regs;
  ASSERT_EQ(zx_thread_read_state(thread_handle, ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)),
            ZX_OK);

  // Check that zx_thread_read_state() does not report any more flag bits
  // than are readable via userland instructions.
  const uint64_t kUserVisibleFlags = 0xf0000000;
  EXPECT_EQ(regs.cpsr & ~kUserVisibleFlags, 0u);

  // Try setting more flag bits.
  uint64_t original_cpsr = regs.cpsr;
  regs.cpsr |= ~kUserVisibleFlags;
  ASSERT_EQ(zx_thread_write_state(thread_handle, ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)),
            ZX_OK);

  // Firstly, if we read back the register flag, the extra flag bits
  // should have been ignored and should not be reported as set.
  ASSERT_EQ(zx_thread_read_state(thread_handle, ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)),
            ZX_OK);
  EXPECT_EQ(regs.cpsr, original_cpsr);

  // Secondly, if we resume the thread, we should be able to kill it.  If
  // zx_thread_write_state() set the interrupt disable flags, then if the
  // thread gets scheduled, it will never get interrupted and we will not
  // be able to kill and join the thread.
  value = 0;
  ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);
  // Wait until the thread has actually resumed execution.
  while (value != 1) {
    ASSERT_EQ(zx_nanosleep(zx_deadline_after(ZX_USEC(1))), ZX_OK);
  }
  ASSERT_EQ(zx_task_kill(thread_handle), ZX_OK);
  ASSERT_EQ(zx_object_wait_one(thread_handle, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL), ZX_OK);

// Clean up.
#endif
}

TEST(Threads, WriteReadDebugRegisterState) {
#if defined(__x86_64__)
  zx_thread_state_debug_regs_t debug_regs_to_write;
  zx_thread_state_debug_regs_t debug_regs_expected;
  debug_regs_fill_test_values(&debug_regs_to_write, &debug_regs_expected);

  // Because setting debug state is priviledged, we need to do it through syscalls:
  // 1. Start the thread into a routine that simply spins idly.
  // 2. Suspend it.
  // 3. Write the expected debug state through a syscall.
  // 4. Resume the thread.
  // 5. Suspend it again.
  // 6. Read the state and compare it.

  RegisterReadSetup<zx_thread_state_debug_regs_t> setup;
  setup.RunUntil(&spin_with_debug_regs, &debug_regs_to_write,
                 reinterpret_cast<uintptr_t>(&spin_address));

  // Write the test values to the debug registers.
  ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS,
                                  &debug_regs_to_write, sizeof(debug_regs_to_write)),
            ZX_OK);

  // Resume and re-suspend the thread.
  setup.Resume();
  setup.Suspend();

  // Get the current debug state of the suspended thread.
  zx_thread_state_debug_regs_t regs;
  ASSERT_EQ(
      zx_thread_read_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS, &regs, sizeof(regs)),
      ZX_OK);

  ASSERT_NO_FATAL_FAILURES(debug_regs_expect_eq(__FILE__, __LINE__, regs, debug_regs_expected));

#elif defined(__aarch64__)
  // We get how many breakpoints we have.
  zx_thread_state_debug_regs_t actual_regs = {};
  RegisterReadSetup<zx_thread_state_debug_regs_t> setup;
  setup.RunUntil(&spin_with_debug_regs, &actual_regs, reinterpret_cast<uintptr_t>(&spin_address));

  ASSERT_EQ(zx_thread_read_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS, &actual_regs,
                                 sizeof(actual_regs)),
            ZX_OK);

  // Arm ensures at least 2 breakpoints.
  ASSERT_GE(actual_regs.hw_bps_count, 2u);
  ASSERT_LE(actual_regs.hw_bps_count, 16u);

  // TODO(donosoc): Once the context switch state tracking is done, add the resume-suspect test
  //                to ensure that it's keeping the state correctly. This is what is done in the
  //                x86 portion of this test.

  zx_thread_state_debug_regs_t regs, expected;
  debug_regs_fill_test_values(&regs, &expected);

  ASSERT_EQ(
      zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS, &regs, sizeof(regs)),
      ZX_OK);
  ASSERT_EQ(
      zx_thread_read_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS, &regs, sizeof(regs)),
      ZX_OK);

  ASSERT_NO_FATAL_FAILURES(debug_regs_expect_eq(__FILE__, __LINE__, regs, expected));
#endif
}

// All writeable bits as 0.
#define DR6_ZERO_MASK (0xffff0ff0ul)
#define DR7_ZERO_MASK (0x700ul)

TEST(Threads, DebugRegistersValidation) {
#if defined(__x86_64__)
  zx_thread_state_debug_regs_t debug_regs = {};
  RegisterReadSetup<zx_thread_state_debug_regs_t> setup;
  setup.RunUntil(&spin_with_debug_regs, &debug_regs, reinterpret_cast<uintptr_t>(&spin_address));

  // Writing all 0s should work and should mask values.
  ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS, &debug_regs,
                                  sizeof(debug_regs)),
            ZX_OK);

  ASSERT_EQ(zx_thread_read_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS, &debug_regs,
                                 sizeof(debug_regs)),
            ZX_OK);

  for (size_t i = 0; i < 4; i++)
    ASSERT_EQ(debug_regs.dr[i], 0);
  ASSERT_EQ(debug_regs.dr6, DR6_ZERO_MASK);
  ASSERT_EQ(debug_regs.dr7, DR7_ZERO_MASK);

  // Writing an invalid address should fail.
  debug_regs = {};
  debug_regs.dr[1] = 0x1000;
  ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS, &debug_regs,
                                  sizeof(debug_regs)),
            ZX_ERR_INVALID_ARGS);

  // Writing an kernel address should fail.
  debug_regs = {};
  debug_regs.dr[2] = 0xffff00000000;
  ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS, &debug_regs,
                                  sizeof(debug_regs)),
            ZX_ERR_INVALID_ARGS);

  // Invalid values should be masked out.
  debug_regs = {};
  debug_regs.dr6 = ~DR6_ZERO_MASK;
  // We avoid the General Detection flag, which would make us throw an exception on next write.
  debug_regs.dr7 = ~DR7_ZERO_MASK;
  ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS, &debug_regs,
                                  sizeof(debug_regs)),
            ZX_OK);

  ASSERT_EQ(zx_thread_read_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS, &debug_regs,
                                 sizeof(debug_regs)),
            ZX_OK);

  for (size_t i = 0; i < 4; i++)
    ASSERT_EQ(debug_regs.dr[i], 0);
  // DR6: Should not have been written.
  ASSERT_EQ(debug_regs.dr6, DR6_ZERO_MASK);
  ASSERT_EQ(debug_regs.dr7, 0xffff07ff);
#elif defined(__aarch64__)
  zx_thread_state_debug_regs_t debug_regs = {};
  zx_thread_state_debug_regs_t actual_regs = {};
  RegisterReadSetup<zx_thread_state_debug_regs_t> setup;
  setup.RunUntil(&spin_with_debug_regs, &actual_regs, reinterpret_cast<uintptr_t>(&spin_address));

  // We read the initial state to know how many HW breakpoints we have.
  ASSERT_EQ(zx_thread_read_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS, &actual_regs,
                                 sizeof(actual_regs)),
            ZX_OK);

  // Writing a kernel address should fail.
  debug_regs.hw_bps_count = actual_regs.hw_bps_count;
  debug_regs.hw_bps[0].dbgbvr = (uint64_t)-1;
  ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS, &debug_regs,
                                  sizeof(debug_regs)),
            ZX_ERR_INVALID_ARGS, "Kernel address should fail");

  // Validation should mask unwanted values from the control register.
  // Only bit 0 is unset. This means the breakpoint is disabled.
  debug_regs.hw_bps[0].dbgbcr = 0xfffffffe;
  debug_regs.hw_bps[0].dbgbvr = 0;  // 0 is a valid value.

  debug_regs.hw_bps[1].dbgbcr = 0x1;  // Only the enabled value is set.
  // We use the address of a function we know is in userspace.
  debug_regs.hw_bps[1].dbgbvr = reinterpret_cast<uint64_t>(wait_thread_blocked);
  ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS, &debug_regs,
                                  sizeof(debug_regs)),
            ZX_OK, "Validation should correctly mask invalid values");

  // Re-read the state and verify.
  ASSERT_EQ(zx_thread_read_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS, &actual_regs,
                                 sizeof(actual_regs)),
            ZX_OK);

  EXPECT_EQ(actual_regs.hw_bps_count, debug_regs.hw_bps_count);
  EXPECT_EQ(actual_regs.hw_bps[0].dbgbcr, 0);
  EXPECT_EQ(actual_regs.hw_bps[0].dbgbvr, 0);
  EXPECT_EQ(actual_regs.hw_bps[1].dbgbcr, 0x000001e5);
  EXPECT_EQ(actual_regs.hw_bps[1].dbgbvr, debug_regs.hw_bps[1].dbgbvr);
#endif
}

// This is a regression test for ZX-4390. Verify that upon entry to the kernel via fault on hardware
// that lacks SMAP, a subsequent usercopy does not panic.
TEST(Threads, X86AcFlagUserCopy) {
#if defined(__x86_64__)
  zx::process process;
  zx::thread thread;
  zx::event event;
  ASSERT_EQ(zx::event::create(0, &event), ZX_OK);
  ASSERT_EQ(start_mini_process(zx_job_default(), event.get(), process.reset_and_get_address(),
                               thread.reset_and_get_address()),
            ZX_OK);

  // Suspend the process so we can set its AC flag.
  zx::handle suspend_token;
  suspend_thread_synchronous(thread.get(), suspend_token.reset_and_get_address());

  zx_thread_state_general_regs_t regs{};
  ASSERT_EQ(thread.read_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)), ZX_OK);

  // Set AC and change its RIP to 0 so that upon resuming, it will fault and enter the kernel.
  regs.rflags |= (1 << 18);
  regs.rip = 0;
  ASSERT_EQ(thread.write_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)), ZX_OK);

  // We can't catch this exception in userspace, the test requires the
  // kernel do a usercopy from an interrupt context which only happens when
  // the exception falls through unhandled.
  printf("Crashing a test process, the following dump is intentional\n");

  // Resume.
  suspend_token.reset();

  // See that it has terminated.
  ASSERT_EQ(process.wait_one(ZX_THREAD_TERMINATED, zx::time::infinite(), nullptr), ZX_OK);
  zx_info_process_t proc_info{};
  ASSERT_EQ(process.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr),
            ZX_OK);
  ASSERT_EQ(proc_info.return_code, ZX_TASK_RETCODE_EXCEPTION_KILL);
#endif
}
