// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <inttypes.h>
#include <lib/test-exceptions/exception-catcher.h>
#include <lib/zx/channel.h>
#include <lib/zx/clock.h>
#include <lib/zx/exception.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/task.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/threads.h>

#include <iterator>
#include <optional>
#include <string>
#include <type_traits>

#include <fbl/algorithm.h>
#include <test-utils/test-utils.h>
#include <zxtest/zxtest.h>

namespace {

static int thread_func(void* arg);

// argv[0]
static char* program_path;

static const char test_child_name[] = "test-child";
static const char exit_closing_excp_handle_child_name[] = "exit-closing-excp-handle";

enum message {
  // Make the type of this enum signed so that we don't get a compile failure
  // later with things like EXPECT_EQ(msg, MSG_PONG) [unsigned vs signed
  // comparison mismatch]
  MSG_ENSURE_SIGNED = -1,
  MSG_DONE,
  MSG_CRASH,
  MSG_PING,
  MSG_PONG,
  MSG_CREATE_AUX_THREAD,
  MSG_AUX_THREAD_HANDLE,
  MSG_CRASH_AUX_THREAD,
  MSG_SHUTDOWN_AUX_THREAD
};

static void crash_me() {
  volatile int* p = 0;
  *p = 42;
}

static void send_msg_new_thread_handle(zx_handle_t handle, zx_handle_t thread) {
  // Note: The handle is transferred to the receiver.
  uint64_t data = MSG_AUX_THREAD_HANDLE;
  zx_status_t status = zx_channel_write(handle, 0, &data, sizeof(data), &thread, 1);
  ZX_DEBUG_ASSERT(status == ZX_OK);
}

static void send_msg(zx_handle_t handle, message msg) {
  uint64_t data = msg;
  zx_status_t status = zx_channel_write(handle, 0, &data, sizeof(data), NULL, 0);
  ZX_DEBUG_ASSERT(status == ZX_OK);
}

static bool recv_msg(zx_handle_t handle, message* msg) {
  uint64_t data;
  uint32_t num_bytes = sizeof(data);

  if (!tu_channel_wait_readable(handle)) {
    return false;
  }

  zx_status_t status =
      zx_channel_read(handle, 0, &data, nullptr, num_bytes, 0, &num_bytes, nullptr);
  if (status != ZX_OK || num_bytes != sizeof(data)) {
    return false;
  }

  *msg = static_cast<message>(data);
  return true;
}

static void recv_msg_new_thread_handle(zx_handle_t handle, zx_handle_t* thread) {
  uint64_t data;
  uint32_t num_bytes = sizeof(data);

  ASSERT_TRUE(tu_channel_wait_readable(handle), "peer closed while trying to read message");

  uint32_t num_handles = 1;
  zx_status_t status =
      zx_channel_read(handle, 0, &data, thread, num_bytes, num_handles, &num_bytes, &num_handles);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(num_bytes, sizeof(data));
  ASSERT_EQ(num_handles, 1u);

  ASSERT_EQ(static_cast<message>(data), MSG_AUX_THREAD_HANDLE);
}

static bool ensure_child_running(zx_handle_t channel) {
  // Note: This function is called from external threads and thus does
  // not use EXPECT_*/ASSERT_*.
  message msg;
  send_msg(channel, MSG_PING);
  if (!recv_msg(channel, &msg)) {
    return false;
  }
  if (msg != MSG_PONG) {
    return false;
  }
  return true;
}

static void msg_loop(zx_handle_t channel) {
  bool my_done_tests = false;
  zx_handle_t channel_to_thread = ZX_HANDLE_INVALID;

  while (!my_done_tests) {
    message msg;
    if (!recv_msg(channel, &msg)) {
      return;
    }
    switch (msg) {
      case MSG_DONE:
        my_done_tests = true;
        break;
      case MSG_CRASH:
        crash_me();
        break;
      case MSG_PING:
        send_msg(channel, MSG_PONG);
        break;
      case MSG_CREATE_AUX_THREAD:
        // Spin up a thread that we can talk to.
        {
          if (channel_to_thread != ZX_HANDLE_INVALID) {
            printf("previous thread connection not shutdown\n");
            return;
          }
          zx_handle_t channel_from_thread;
          zx_status_t status = zx_channel_create(0, &channel_to_thread, &channel_from_thread);
          ZX_DEBUG_ASSERT(status == ZX_OK);
          thrd_t thread;
          int ret = thrd_create_with_name(
              &thread, thread_func, (void*)(uintptr_t)channel_from_thread, "msg-loop-subthread");
          ZX_DEBUG_ASSERT(ret == thrd_success);
          // Make sure the new thread is up and running before sending
          // its handle back: this removes potential problems like
          // needing to handle ZX_EXCP_THREAD_STARTING exceptions if the
          // debugger exception channel is bound later.
          if (ensure_child_running(channel_to_thread)) {
            zx_handle_t thread_handle = thrd_get_zx_handle(thread);
            zx_handle_t copy = ZX_HANDLE_INVALID;
            zx_status_t status = zx_handle_duplicate(thread_handle, ZX_RIGHT_SAME_RIGHTS, &copy);
            ZX_DEBUG_ASSERT(status == ZX_OK);
            send_msg_new_thread_handle(channel, copy);
          } else {
            // We could terminate the thread or some such, but the
            // process will be killed by our "caller".
            send_msg_new_thread_handle(channel, ZX_HANDLE_INVALID);
            zx_handle_close(channel_to_thread);
            channel_to_thread = ZX_HANDLE_INVALID;
          }
        }
        break;
      case MSG_CRASH_AUX_THREAD:
        send_msg(channel_to_thread, MSG_CRASH);
        break;
      case MSG_SHUTDOWN_AUX_THREAD:
        send_msg(channel_to_thread, MSG_DONE);
        zx_handle_close(channel_to_thread);
        channel_to_thread = ZX_HANDLE_INVALID;
        break;
      default:
        printf("unknown message received: %d\n", msg);
        break;
    }
  }
}

static int thread_func(void* arg) {
  zx_handle_t msg_channel = (zx_handle_t)(uintptr_t)arg;
  msg_loop(msg_channel);
  zx_handle_close(msg_channel);
  return 0;
}

static void __NO_RETURN test_child() {
  zx_handle_t channel = zx_take_startup_handle(PA_USER0);
  if (channel == ZX_HANDLE_INVALID)
    tu_fatal("zx_take_startup_handle", ZX_ERR_BAD_HANDLE - 1000);
  msg_loop(channel);
  exit(0);
}

static springboard_t* setup_test_child(zx_handle_t job, const char* arg, zx_handle_t* out_channel) {
  zx_handle_t our_channel, their_channel;
  zx_status_t status = zx_channel_create(0, &our_channel, &their_channel);
  ZX_DEBUG_ASSERT(status == ZX_OK);
  const char* test_child_path = program_path;
  const char* const argv[] = {
      test_child_path,
      arg,
  };
  int argc = std::size(argv);
  zx_handle_t handles[1] = {their_channel};
  uint32_t handle_ids[1] = {PA_USER0};
  *out_channel = our_channel;
  springboard_t* sb =
      tu_launch_init(job, test_child_name, argc, argv, 0, NULL, 1, handles, handle_ids);
  return sb;
}

static void start_test_child_with_exception_channel(const zx::job& job, const std::string& arg,
                                                    zx::process* out_child,
                                                    zx::channel* out_exception_channel,
                                                    zx::channel* out_channel) {
  springboard_t* sb =
      setup_test_child(job.get(), arg.c_str(), out_channel->reset_and_get_address());
  ASSERT_OK(zx_task_create_exception_channel(springboard_get_process_handle(sb),
                                             ZX_EXCEPTION_CHANNEL_DEBUGGER,
                                             out_exception_channel->reset_and_get_address()));
  out_child->reset(tu_launch_fini(sb));
}

struct proc_handles {
  zx_handle_t proc;
  zx_handle_t vmar;
};

// Waits for and reads an exception.
//
// If |type| is valid, checks that the received exception matches.
// If |info| is non-null, fills it in with the received struct.
//
// Returns an invalid exception and marks test failure on error or if |type|
// doesn't match.
zx::exception ReadException(const zx::channel& channel,
                            std::optional<zx_excp_type_t> type = std::nullopt,
                            zx_exception_info_t* info_out = nullptr) {
  zx::exception exception;
  zx_exception_info_t info;
  uint32_t num_handles = 1;
  uint32_t num_bytes = sizeof(info);

  zx_status_t status = channel.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), nullptr);
  if (status != ZX_OK) {
    EXPECT_OK(status);
    return zx::exception();
  }

  status = zx_channel_read(channel.get(), 0, &info, exception.reset_and_get_address(), num_bytes,
                           num_handles, &num_bytes, &num_handles);
  EXPECT_EQ(status, ZX_OK);
  if (!exception.is_valid()) {
    EXPECT_TRUE(exception.is_valid());
    return zx::exception();
  }

  if (info_out != nullptr) {
    *info_out = info;
  }

  if (type && type != info.type) {
    EXPECT_EQ(type, info.type);
    return zx::exception();
  }
  return exception;
}

static void __NO_RETURN trigger_unsupported() {
  // An unsupported exception is not a failure.
  // Generally it just means that support for the exception doesn't
  // exist yet on this particular architecture.
  exit(0);
}

static void __NO_RETURN trigger_general() {
#if defined(__x86_64__)
#elif defined(__aarch64__)
#endif
  trigger_unsupported();
}

static void __NO_RETURN trigger_fatal_page_fault() {
  *(volatile int*)0 = 42;
  trigger_unsupported();
}

static void __NO_RETURN trigger_undefined_insn() {
#if defined(__x86_64__)
  __asm__("ud2");
#elif defined(__aarch64__)
  // An instruction not supported at this privilege level will do.
  // ARM calls these "unallocated instructions". Geez, "unallocated"?
  __asm__("mrs x0, elr_el1");
#endif
  trigger_unsupported();
}

static void __NO_RETURN trigger_sw_bkpt() {
#if defined(__x86_64__)
  __asm__("int3");
#elif defined(__aarch64__)
  __asm__("brk 0");
#endif
  trigger_unsupported();
}

static void __NO_RETURN trigger_hw_bkpt() {
#if defined(__x86_64__)
  // We can't set the debug regs from user space, support for setting the
  // debug regs via the debugger interface is work-in-progress, and we can't
  // use "int $1" here. So testing this will have to wait.
#elif defined(__aarch64__)
#endif
  trigger_unsupported();
}

#if defined(__aarch64__)
static void __NO_RETURN trigger_arm64_wfi() {
  // WFI is illegal in user space
  __asm__("wfi");
  __asm__("wfi");
  trigger_unsupported();
}

static void __NO_RETURN trigger_arm64_wfe() {
  // WFE is legal in user space
  // Run it twice in a row in case the event is already set and it is naturally
  // 'falling through'.
  __asm__("wfe");
  __asm__("wfe");
  trigger_unsupported();
}

#endif

// ARM does not trap on integer divide-by-zero.
#if defined(__x86_64__)
static void __NO_RETURN trigger_integer_divide_by_zero() {
  // Use an x86 division instruction (rather than doing division from C)
  // to ensure that the compiler does not convert the division into
  // something else.
  uint32_t result;
  __asm__ volatile("idivb %1" : "=a"(result) : "r"((uint8_t)0), "a"((uint16_t)1));
  trigger_unsupported();
}

static void __NO_RETURN trigger_sse_divide_by_zero() {
  // Unmask all exceptions for SSE operations.
  uint32_t mxcsr = 0;
  __asm__ volatile("ldmxcsr %0" : : "m"(mxcsr));

  double a = 1;
  double b = 0;
  __asm__ volatile("divsd %1, %0" : "+x"(a) : "x"(b));

  // QEMU's software emulation of x86 appears to have a bug where it does
  // not correctly emulate generating division-by-zero exceptions from
  // SSE instructions.  See https://bugs.launchpad.net/qemu/+bug/1668041.
  // So we will reach this point on non-KVM QEMU.  In this case, make the
  // test pass by generating a fault by other means.
  //
  // That means this test isn't requiring that "divsd" generates a fault.
  // It is only requiring that the fault is handled properly
  // (e.g. doesn't cause a kernel panic) if the instruction does fault
  // (as on real hardware).
  printf(
      "trigger_sse_divide_by_zero: divsd did not fault; "
      "assume we are running under a buggy non-KVM QEMU\n");
  trigger_integer_divide_by_zero();
}

static void __NO_RETURN trigger_x87_divide_by_zero() {
  // Unmask all exceptions for x87 operations.
  uint16_t control_word = 0;
  __asm__ volatile("fldcw %0" : : "m"(control_word));

  double a = 1;
  double b = 0;
  __asm__ volatile(
      "fldl %0\n"
      "fdivl %1\n"
      // Check for the pending exception.
      "fwait\n"
      :
      : "m"(a), "m"(b));
  trigger_unsupported();
}
#endif

static const struct {
  zx_excp_type_t type;
  const char* name;
  bool crashes;
  void __NO_RETURN (*trigger_function)();
} exceptions[] = {
    {ZX_EXCP_GENERAL, "general", false, trigger_general},
    {ZX_EXCP_FATAL_PAGE_FAULT, "page-fault", true, trigger_fatal_page_fault},
    {ZX_EXCP_UNDEFINED_INSTRUCTION, "undefined-insn", true, trigger_undefined_insn},
    {ZX_EXCP_SW_BREAKPOINT, "sw-bkpt", true, trigger_sw_bkpt},
    {ZX_EXCP_HW_BREAKPOINT, "hw-bkpt", false, trigger_hw_bkpt},
#if defined(__x86_64__)
    {ZX_EXCP_GENERAL, "integer-divide-by-zero", true, trigger_integer_divide_by_zero},
    {ZX_EXCP_GENERAL, "sse-divide-by-zero", true, trigger_sse_divide_by_zero},
    {ZX_EXCP_GENERAL, "x87-divide-by-zero", true, trigger_x87_divide_by_zero},
#endif
#if defined(__aarch64__)
    {ZX_EXCP_GENERAL, "arm64-wfi", true, trigger_arm64_wfi},
    {ZX_EXCP_GENERAL, "arm64-wfe", false, trigger_arm64_wfe},
#endif
};

static void __NO_RETURN trigger_exception(const char* excp_name) {
  for (size_t i = 0; i < std::size(exceptions); ++i) {
    if (strcmp(excp_name, exceptions[i].name) == 0) {
      exceptions[i].trigger_function();
    }
  }
  fprintf(stderr, "unknown exception: %s\n", excp_name);
  exit(1);
}

static void __NO_RETURN test_child_trigger(const char* excp_name) {
  trigger_exception(excp_name);
  /* NOTREACHED */
}

TEST(ExceptionTest, Trigger) {
  for (size_t i = 0; i < std::size(exceptions); ++i) {
    zx_excp_type_t excp_type = exceptions[i].type;
    const char* excp_name = exceptions[i].name;
    zx::process child;
    zx::channel exception_channel, our_channel;
    std::string arg = std::string("trigger=") + excp_name;
    start_test_child_with_exception_channel(*zx::job::default_job(), arg, &child,
                                            &exception_channel, &our_channel);

    test_exceptions::ExceptionCatcher catcher(*zx::job::default_job());

    // First read the THREAD_STARTING exception. We can just discard it
    // immediately since THREAD_STARTING doesn't care whether it's resumed or
    // not.
    zx_exception_info_t info;
    ReadException(exception_channel, ZX_EXCP_THREAD_STARTING, &info);
    const zx_koid_t tid = info.tid;

    // This can be |excp_type| or THREAD_EXITING if |excp_type| is unsupported.
    zx::exception exception = ReadException(exception_channel, std::nullopt, &info);
    ASSERT_EQ(tid, info.tid);

    if (info.type != ZX_EXCP_THREAD_EXITING) {
      ASSERT_EQ(excp_type, info.type);
      exception.reset();

      if (exceptions[i].crashes) {
        zx::status<zx::exception> result = catcher.ExpectException(child);
        ASSERT_TRUE(result.is_ok());
        ASSERT_OK(child.kill());
      }

      exception = ReadException(exception_channel, ZX_EXCP_THREAD_EXITING, &info);
      ASSERT_EQ(tid, info.tid);
    }

    // We've already seen tid's thread-exit report, so just skip that
    // test here.
    exception.reset();
    EXPECT_OK(child.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr));
  }
}

static void test_child_exit_closing_excp_handle() {
  // Test ZX-1544. Process termination closing the last handle of the exception
  // channel should not cause a panic.
  zx::channel exception_channel;
  ASSERT_OK(zx::process::self()->create_exception_channel(0, &exception_channel));
  exit(0);

  /* NOTREACHED */
}

TEST(ExceptionTest, ExitClosingExcpHandle) {
  const char* test_child_path = program_path;
  const char* const argv[] = {
      test_child_path,
      exit_closing_excp_handle_child_name,
  };
  int argc = std::size(argv);

  springboard_t* sb = tu_launch_init(zx_job_default(), exit_closing_excp_handle_child_name, argc,
                                     argv, 0, NULL, 0, NULL, NULL);
  zx_handle_t child = tu_launch_fini(sb);

  zx_signals_t signals = ZX_PROCESS_TERMINATED;
  zx_signals_t pending;
  EXPECT_OK(zx_object_wait_one(child, signals, ZX_TIME_INFINITE, &pending));
  EXPECT_TRUE(pending & ZX_PROCESS_TERMINATED);

  EXPECT_EQ(tu_process_get_return_code(child), 0);
}

// Same as send_msg() but also allows ZX_ERR_PEER_CLOSED.
// Useful for generic test cleanup to handle both live and killed tasks.
static void SendMessageOrPeerClosed(const zx::channel& channel, message msg) {
  uint64_t data = msg;
  zx_status_t status = channel.write(0, &data, sizeof(data), nullptr, 0);
  if (status != ZX_OK && status != ZX_ERR_PEER_CLOSED) {
    tu_fatal(__func__, status);
  }
}

// C++ wrapper for our testing message loop to remove common boilerplate.
//
// Creates this test loop task structure under the current job:
//   - parent job
//     - job
//       - process
//         - thread
//         - aux thread
class TestLoop {
 public:
  enum class Control { kAutomatic, kManual };

  // TestLoop can operate in two different modes:
  //
  // Automatic control will take care of all the setup/teardown so that when
  // this constructor returns the test threads will be running, and when
  // the destructor is called they will be stopped and closed down.
  //
  // Manual control requires the caller to make the following calls in order:
  //   - Step1CreateProcess()
  //   - Step2StartThreads()
  //   - Step3FinishSetup()
  //   - Step4ShutdownAuxThread()
  //   - Step5ShutdownMainThread()
  // This is necessary to give the caller a chance to install exception
  // handlers in between each step, e.g. in order to catch THREAD_STARTING
  // synthetic exceptions.
  TestLoop(Control control = Control::kAutomatic) {
    EXPECT_OK(zx::job::create(*zx::job::default_job(), 0, &parent_job_));
    EXPECT_OK(zx::job::create(parent_job_, 0, &job_));

    if (control == Control::kAutomatic) {
      Step1CreateProcess();
      Step2StartThreads();
      Step3ReadAuxThreadHandle();
    }
  }

  void Step1CreateProcess() {
    springboard_ =
        setup_test_child(job_.get(), test_child_name, process_channel_.reset_and_get_address());
    ASSERT_NOT_NULL(springboard_);
    process_.reset(springboard_get_process_handle(springboard_));
  }

  void Step2StartThreads() {
    // The initial process handle we got is invalidated by this call
    // and we're given the new one to use instead.
    zx_handle_t process = tu_launch_fini(springboard_);
    if (process != process_.get()) {
      process_.reset(process);
    }
    ASSERT_TRUE(process_.is_valid());
    send_msg(process_channel_.get(), MSG_CREATE_AUX_THREAD);
  }

  // If there are any debugger handlers attached, the task start exceptions
  // must be handled before calling this or it will block forever.
  void Step3ReadAuxThreadHandle() {
    recv_msg_new_thread_handle(process_channel_.get(), aux_thread_.reset_and_get_address());
  }

  void Step4ShutdownAuxThread() {
    // Don't use use zx_task_kill() here, it stops exception processing
    // immediately so we may miss expected exceptions.
    SendMessageOrPeerClosed(process_channel_, MSG_SHUTDOWN_AUX_THREAD);
  }

  void Step5ShutdownMainThread() { SendMessageOrPeerClosed(process_channel_, MSG_DONE); }

  // Closes the test tasks and blocks until everything has cleaned up.
  //
  // If there is an active debug handler, the process must be closed first
  // via zx_task_kill() or Shutdown(), or else this can block forever waiting
  // for the thread exit exceptions to be handled.
  ~TestLoop() {
    // It's OK to call these multiple times so we can just unconditionally
    // call them in both automatic or manual control mode.
    Step4ShutdownAuxThread();
    Step5ShutdownMainThread();

    EXPECT_OK(process_.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr));
  }

  const zx::job& parent_job() const { return parent_job_; }
  const zx::job& job() const { return job_; }
  const zx::process& process() const { return process_; }
  const zx::thread& aux_thread() const { return aux_thread_; }

  // Sends a message to the aux thread to crash itself.
  //
  // If this is used, before exiting the test either kill the aux thread or
  // pass the exception to the unittest crash handler and block until it
  // kills the thread.
  //
  // The blocking is important because otherwise there's a race where the loop
  // process main thread can exit and kill the aux thread before the crash
  // handler gets a chance to see the exception. If this happens, the crash
  // handler will notice there was a registered exception that never occurred
  // and will fail the test.
  void CrashAuxThread() { send_msg(process_channel_.get(), MSG_CRASH_AUX_THREAD); }

 private:
  springboard* springboard_ = nullptr;
  zx::job parent_job_;
  zx::job job_;
  zx::process process_;
  zx::channel process_channel_;
  zx::thread aux_thread_;
};

// Returns true if the exception has a thread handle. If |koid| is given,
// also checks that the thread's koid matches it.
bool ExceptionHasThread(const zx::exception& exception, zx_koid_t koid = ZX_KOID_INVALID) {
  zx::thread thread;
  if (exception.get_thread(&thread) != ZX_OK) {
    return false;
  }
  if (koid == ZX_KOID_INVALID) {
    return true;
  }
  zx_info_handle_basic_t info;
  zx_status_t status = thread.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  ZX_ASSERT(status == ZX_OK);
  return koid == info.koid;
}

// Returns true if the exception has a process handle. If |koid| is given,
// also checks that the process' koid matches it.
bool ExceptionHasProcess(const zx::exception& exception, zx_koid_t koid = ZX_KOID_INVALID) {
  zx::process process;
  if (exception.get_process(&process) != ZX_OK) {
    return false;
  }
  if (koid == ZX_KOID_INVALID) {
    return true;
  }
  zx_info_handle_basic_t info;
  zx_status_t status =
      process.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  ZX_ASSERT(status == ZX_OK);
  return koid == info.koid;
}

uint32_t GetExceptionStateProperty(const zx::exception& exception) {
  uint32_t state = ~0;
  EXPECT_OK(exception.get_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state)));
  return state;
}

void SetExceptionStateProperty(const zx::exception& exception, uint32_t state) {
  ASSERT_OK(exception.set_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state)));
}

uint32_t GetExceptionStrategyProperty(const zx::exception& exception) {
  uint32_t state = ~0;
  EXPECT_OK(exception.get_property(ZX_PROP_EXCEPTION_STRATEGY, &state, sizeof(state)));
  return state;
}

void SetExceptionStrategyProperty(const zx::exception& exception, uint32_t state) {
  ASSERT_OK(exception.set_property(ZX_PROP_EXCEPTION_STRATEGY, &state, sizeof(state)));
}

// A finite timeout to use when you want to make sure something isn't happening
// e.g. a certain signal isn't going to be asserted.
auto constexpr kTestTimeout = zx::msec(50);

TEST(ExceptionTest, CreateExceptionChannel) {
  TestLoop loop;

  zx::channel exception_channel;
  ASSERT_OK(loop.aux_thread().create_exception_channel(0u, &exception_channel));
  EXPECT_TRUE(exception_channel.is_valid());
}

TEST(ExceptionTest, CreateExceptionChannelRights) {
  TestLoop loop;

  zx::channel exception_channel;
  ASSERT_OK(loop.aux_thread().create_exception_channel(0, &exception_channel));

  zx_info_handle_basic_t info;
  ASSERT_OK(
      exception_channel.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));

  // If this set of rights ever changes make sure to adjust the
  // task_create_exception_channel() documentation as well.
  EXPECT_EQ(info.rights, ZX_RIGHT_TRANSFER | ZX_RIGHT_WAIT | ZX_RIGHT_READ);
}

TEST(ExceptionTest, CreateExceptionChannelInvalidArgs) {
  TestLoop loop;

  zx::channel exception_channel;
  EXPECT_EQ(
      loop.aux_thread().create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER, &exception_channel),
      ZX_ERR_INVALID_ARGS);
}

TEST(ExceptionTest, ProcessDebuggerAttached) {
  TestLoop loop;

  zx_info_process_t info;
  ASSERT_OK(loop.process().get_info(ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr));
  EXPECT_FALSE(info.debugger_attached);

  {
    zx::channel exception_channel;
    ASSERT_OK(
        loop.process().create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER, &exception_channel));

    ASSERT_OK(loop.process().get_info(ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr));
    EXPECT_TRUE(info.debugger_attached);
  }

  ASSERT_OK(loop.process().get_info(ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr));
  EXPECT_FALSE(info.debugger_attached);
}

// Removes a right from a task and ensures that channel creation now fails.
//
// |task_func|: TestLoop member function to get the task.
// |right|: ZX_RIGHT_* value to remove.
template <auto task_func>
void TaskRequiresRight(zx_rights_t right) {
  TestLoop loop;
  const auto& task = (loop.*task_func)();

  zx_info_handle_basic_t info;
  zx_status_t status = task.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  ASSERT_EQ(status, ZX_OK);

  auto reduced_task = typename std::remove_reference<decltype(task)>::type();
  ASSERT_OK(task.duplicate(info.rights & ~right, &reduced_task));

  zx::channel exception_channel;
  EXPECT_EQ(reduced_task.create_exception_channel(0, &exception_channel), ZX_ERR_ACCESS_DENIED);
}

TEST(ExceptionTest, ThreadRequiresRights) {
  ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::aux_thread>(ZX_RIGHT_INSPECT));
  ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::aux_thread>(ZX_RIGHT_DUPLICATE));
  ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::aux_thread>(ZX_RIGHT_TRANSFER));
  ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::aux_thread>(ZX_RIGHT_MANAGE_THREAD));
}

TEST(ExceptionTest, ProcessRequiresRights) {
  ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::process>(ZX_RIGHT_INSPECT));
  ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::process>(ZX_RIGHT_DUPLICATE));
  ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::process>(ZX_RIGHT_TRANSFER));
  ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::process>(ZX_RIGHT_MANAGE_THREAD));
  ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::process>(ZX_RIGHT_ENUMERATE));
}

TEST(ExceptionTest, JobRequiresRights) {
  ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::job>(ZX_RIGHT_INSPECT));
  ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::job>(ZX_RIGHT_DUPLICATE));
  ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::job>(ZX_RIGHT_TRANSFER));
  ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::job>(ZX_RIGHT_MANAGE_THREAD));
  ASSERT_NO_FAILURES(TaskRequiresRight<&TestLoop::job>(ZX_RIGHT_ENUMERATE));
}

TEST(ExceptionTest, CreateSecondExceptionChannel) {
  TestLoop loop;
  zx::channel exception_channel;
  ASSERT_OK(loop.aux_thread().create_exception_channel(0u, &exception_channel));

  // Trying to register a second channel should fail.
  zx::channel exception_channel2;
  EXPECT_EQ(loop.aux_thread().create_exception_channel(0u, &exception_channel2),
            ZX_ERR_ALREADY_BOUND);
  EXPECT_FALSE(exception_channel2.is_valid());
}

TEST(ExceptionTest, OverwriteClosedExceptionChannel) {
  TestLoop loop;
  zx::channel exception_channel;
  ASSERT_OK(loop.aux_thread().create_exception_channel(0u, &exception_channel));

  // If we close the existing channel, registering a new one should succeed.
  exception_channel.reset();
  zx::channel exception_channel2;
  ASSERT_OK(loop.aux_thread().create_exception_channel(0u, &exception_channel2));
  EXPECT_TRUE(exception_channel2.is_valid());
}

// This is the basic test to receive an exception, parameterized so we can
// easily run it against all the different exception handler types.
//
// |task_func|: TestLoop member function to get the task.
// |create_flags|: flags to pass to zx_task_create_exception_channel().
// |expected_type|: expected exception type reported in zx_info_thread_t.
// |has_process|: true if the exception should have a process handle.
template <auto task_func>
void receive_test(uint32_t create_flags, uint32_t expected_type, bool has_process) {
  TestLoop loop;
  zx::channel exception_channel;
  ASSERT_OK((loop.*task_func)().create_exception_channel(create_flags, &exception_channel));

  loop.CrashAuxThread();
  zx_exception_info_t exception_info;
  zx::exception exception =
      ReadException(exception_channel, ZX_EXCP_FATAL_PAGE_FAULT, &exception_info);

  // Make sure exception info is correct.
  zx_info_handle_basic_t basic_info;
  zx_status_t status = loop.aux_thread().get_info(ZX_INFO_HANDLE_BASIC, &basic_info,
                                                  sizeof(basic_info), nullptr, nullptr);
  ASSERT_EQ(status, ZX_OK);
  zx_koid_t aux_thread_koid = basic_info.koid;
  EXPECT_EQ(exception_info.tid, aux_thread_koid);
  EXPECT_TRUE(ExceptionHasThread(exception, exception_info.tid));

  status = loop.process().get_info(ZX_INFO_HANDLE_BASIC, &basic_info, sizeof(basic_info), nullptr,
                                   nullptr);
  ASSERT_EQ(status, ZX_OK);
  zx_koid_t process_koid = basic_info.koid;
  EXPECT_EQ(exception_info.pid, process_koid);
  if (has_process) {
    EXPECT_TRUE(ExceptionHasProcess(exception, exception_info.pid));
  } else {
    EXPECT_FALSE(ExceptionHasProcess(exception));
  }

  // Make sure the thread state is correct.
  zx_info_thread_t thread_info;
  status = loop.aux_thread().get_info(ZX_INFO_THREAD, &thread_info, sizeof(thread_info), nullptr,
                                      nullptr);
  ASSERT_EQ(status, ZX_OK);
  EXPECT_EQ(thread_info.state, ZX_THREAD_STATE_BLOCKED_EXCEPTION);
  EXPECT_EQ(thread_info.wait_exception_channel_type, expected_type);

  test_exceptions::ExceptionCatcher catcher(*zx::job::default_job());
  exception.reset();
  zx::status<zx::exception> result = catcher.ExpectException(loop.aux_thread());
  ASSERT_TRUE(result.is_ok());
  EXPECT_OK(loop.process().kill());
}

TEST(ExceptionTest, ThreadReceive) {
  receive_test<&TestLoop::aux_thread>(0, ZX_EXCEPTION_CHANNEL_TYPE_THREAD, false);
}

TEST(ExceptionTest, ProcessReceive) {
  receive_test<&TestLoop::process>(0, ZX_EXCEPTION_CHANNEL_TYPE_PROCESS, true);
}

TEST(ExceptionTest, ProcessDebuggerReceive) {
  receive_test<&TestLoop::process>(ZX_EXCEPTION_CHANNEL_DEBUGGER,
                                   ZX_EXCEPTION_CHANNEL_TYPE_DEBUGGER, true);
}

TEST(ExceptionTest, JobReceive) {
  receive_test<&TestLoop::job>(0, ZX_EXCEPTION_CHANNEL_TYPE_JOB, true);
}

TEST(ExceptionTest, JobDebuggerReceive) {
  receive_test<&TestLoop::parent_job>(0, ZX_EXCEPTION_CHANNEL_TYPE_JOB, true);
}

TEST(ExceptionTest, ExceptionResume) {
  TestLoop loop;
  zx::channel exception_channel;
  ASSERT_OK(loop.aux_thread().create_exception_channel(0u, &exception_channel));

  loop.CrashAuxThread();
  zx::exception exception = ReadException(exception_channel, ZX_EXCP_FATAL_PAGE_FAULT);

  // If we tell this exception to resume the thread, it should fault
  // again and return another exception back to us rather than
  // bubbling up the chain.
  SetExceptionStateProperty(exception, ZX_EXCEPTION_STATE_HANDLED);
  exception.reset();
  exception = ReadException(exception_channel, ZX_EXCP_FATAL_PAGE_FAULT);

  // Close the new exception without marking it handled so it bubbles up.
  test_exceptions::ExceptionCatcher catcher(loop.process());
  exception.reset();
  zx::status<zx::exception> result = catcher.ExpectException(loop.aux_thread());
  ASSERT_TRUE(result.is_ok());
  EXPECT_OK(loop.process().kill());
}

TEST(ExceptionTest, ExceptionStateProperty) {
  TestLoop loop;
  zx::channel exception_channel;
  ASSERT_OK(loop.aux_thread().create_exception_channel(0u, &exception_channel));

  loop.CrashAuxThread();
  zx::exception exception = ReadException(exception_channel, ZX_EXCP_FATAL_PAGE_FAULT);

  // By default exceptions should be unhandled.
  EXPECT_EQ(GetExceptionStateProperty(exception), ZX_EXCEPTION_STATE_TRY_NEXT);

  SetExceptionStateProperty(exception, ZX_EXCEPTION_STATE_HANDLED);
  EXPECT_EQ(GetExceptionStateProperty(exception), ZX_EXCEPTION_STATE_HANDLED);

  SetExceptionStateProperty(exception, ZX_EXCEPTION_STATE_TRY_NEXT);
  EXPECT_EQ(GetExceptionStateProperty(exception), ZX_EXCEPTION_STATE_TRY_NEXT);

  test_exceptions::ExceptionCatcher catcher(loop.process());
  exception.reset();
  zx::status<zx::exception> result = catcher.ExpectException(loop.aux_thread());
  ASSERT_TRUE(result.is_ok());
  EXPECT_OK(loop.process().kill());
}

TEST(ExceptionTest, ExceptionStatePropertyBadArgs) {
  TestLoop loop;
  zx::channel exception_channel;
  ASSERT_OK(loop.aux_thread().create_exception_channel(0u, &exception_channel));

  loop.CrashAuxThread();
  zx::exception exception = ReadException(exception_channel, ZX_EXCP_FATAL_PAGE_FAULT);

  // Wrong handle type.
  uint32_t state = ZX_EXCEPTION_STATE_HANDLED;
  EXPECT_EQ(loop.aux_thread().set_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state)),
            ZX_ERR_WRONG_TYPE);
  EXPECT_EQ(loop.aux_thread().get_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state)),
            ZX_ERR_WRONG_TYPE);

  // Illegal state value.
  state = ~0;
  EXPECT_EQ(exception.set_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state)),
            ZX_ERR_INVALID_ARGS);

  // Buffer too short.
  state = ZX_EXCEPTION_STATE_HANDLED;
  EXPECT_EQ(exception.set_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state) - 1),
            ZX_ERR_BUFFER_TOO_SMALL);
  EXPECT_EQ(exception.get_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state) - 1),
            ZX_ERR_BUFFER_TOO_SMALL);

  test_exceptions::ExceptionCatcher catcher(loop.process());
  exception.reset();
  zx::status<zx::exception> result = catcher.ExpectException(loop.aux_thread());
  ASSERT_TRUE(result.is_ok());
  EXPECT_OK(loop.process().kill());
}

TEST(ExceptionTest, ExceptionStrategy) {
  TestLoop loop;
  zx::channel exception_channel;
  ASSERT_OK(
      loop.process().create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER, &exception_channel));

  loop.CrashAuxThread();
  zx::exception exception = ReadException(exception_channel, ZX_EXCP_FATAL_PAGE_FAULT);

  // By default exceptions should be first-chance.
  EXPECT_EQ(GetExceptionStrategyProperty(exception), ZX_EXCEPTION_STRATEGY_FIRST_CHANCE);

  SetExceptionStrategyProperty(exception, ZX_EXCEPTION_STRATEGY_SECOND_CHANCE);
  EXPECT_EQ(GetExceptionStrategyProperty(exception), ZX_EXCEPTION_STRATEGY_SECOND_CHANCE);

  // Exception strategy values are independent of state values.
  SetExceptionStateProperty(exception, ZX_EXCEPTION_STATE_HANDLED);
  EXPECT_EQ(GetExceptionStrategyProperty(exception), ZX_EXCEPTION_STRATEGY_SECOND_CHANCE);
  SetExceptionStateProperty(exception, ZX_EXCEPTION_STATE_TRY_NEXT);
  EXPECT_EQ(GetExceptionStrategyProperty(exception), ZX_EXCEPTION_STRATEGY_SECOND_CHANCE);

  test_exceptions::ExceptionCatcher catcher(loop.process());
  exception.reset();
  zx::status<zx::exception> result = catcher.ExpectException(loop.aux_thread());
  ASSERT_TRUE(result.is_ok());
  EXPECT_OK(loop.process().kill());
}

TEST(ExceptionTest, ExceptionStrategyBadArgs) {
  TestLoop loop;
  zx::channel exception_channel;
  ASSERT_OK(loop.aux_thread().create_exception_channel(0u, &exception_channel));
  loop.CrashAuxThread();

  zx::exception exception = ReadException(exception_channel, ZX_EXCP_FATAL_PAGE_FAULT);

  // Second chance property can only be set on a channel associated with a
  // process debugger.
  uint32_t state = ZX_EXCEPTION_STRATEGY_SECOND_CHANCE;
  EXPECT_EQ(exception.set_property(ZX_PROP_EXCEPTION_STRATEGY, &state, sizeof(state)),
            ZX_ERR_BAD_STATE);

  test_exceptions::ExceptionCatcher catcher(loop.process());
  exception.reset();
  zx::status<zx::exception> result = catcher.ExpectException(loop.aux_thread());
  ASSERT_TRUE(result.is_ok());
  EXPECT_OK(loop.process().kill());
}

TEST(ExceptionTest, CloseChannelWithException) {
  TestLoop loop;
  zx::channel exception_channel;
  ASSERT_OK(loop.aux_thread().create_exception_channel(0u, &exception_channel));

  loop.CrashAuxThread();
  ASSERT_OK(exception_channel.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), nullptr));

  // Closing the channel while it still contains the exception should pass
  // control to the next handler.
  test_exceptions::ExceptionCatcher catcher(loop.process());
  exception_channel.reset();
  zx::status<zx::exception> result = catcher.ExpectException(loop.aux_thread());
  ASSERT_TRUE(result.is_ok());
  EXPECT_OK(loop.process().kill());
}

TEST(ExceptionTest, CloseChannelWithoutException) {
  TestLoop loop;
  zx::channel exception_channel;
  ASSERT_OK(loop.aux_thread().create_exception_channel(0u, &exception_channel));

  loop.CrashAuxThread();
  zx::exception exception = ReadException(exception_channel, ZX_EXCP_FATAL_PAGE_FAULT);

  // Closing the channel after the exception object has been read out has no
  // effect since the exception object now controls the exception lifecycle.
  exception_channel.reset();

  // Wait a little bit to make sure the thread really is still blocked on our
  // exception object. If it wasn't, the exception would filter up now and
  // ExpectException() will deadlock when it fails to find the exception.
  zx::nanosleep(zx::deadline_after(kTestTimeout));

  test_exceptions::ExceptionCatcher catcher(loop.process());
  exception.reset();
  zx::status<zx::exception> result = catcher.ExpectException(loop.aux_thread());
  ASSERT_TRUE(result.is_ok());
  EXPECT_OK(loop.process().kill());
}

// Make sure a closed exception channel has no effect on other handlers.
TEST(ExceptionTest, SkipClosedExceptionChannel) {
  TestLoop loop;
  zx::channel job_channel, process_channel;
  ASSERT_OK(loop.job().create_exception_channel(0, &job_channel));
  ASSERT_OK(loop.process().create_exception_channel(0, &process_channel));

  {
    zx::channel thread_channel;
    ASSERT_OK(loop.aux_thread().create_exception_channel(0, &thread_channel));
  }

  loop.CrashAuxThread();

  // We should receive the exception on the process handler and it should
  // wait for our response as normal.
  {
    zx::exception exception = ReadException(process_channel, ZX_EXCP_FATAL_PAGE_FAULT);
    ASSERT_EQ(job_channel.wait_one(ZX_CHANNEL_READABLE, zx::deadline_after(kTestTimeout), nullptr),
              ZX_ERR_TIMED_OUT);
  }

  // The exception should continue up to the job handler as normal.
  zx::exception exception = ReadException(job_channel, ZX_EXCP_FATAL_PAGE_FAULT);

  ASSERT_OK(loop.process().kill());
}

// Killing the task should mark its exception channels with PEER_CLOSED.
// Parameterized to more easily run it against the different handler types.
template <auto task_func>
void TaskDeathClosesExceptionChannel(uint32_t create_flags) {
  TestLoop loop;
  const auto& task = (loop.*task_func)();
  zx::channel exception_channel;
  ASSERT_OK(task.create_exception_channel(create_flags, &exception_channel));

  ASSERT_OK(task.kill());
  EXPECT_OK(exception_channel.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), nullptr));
}

TEST(ExceptionTest, TaskDeathClosesProcessExceptionChannel) {
  TaskDeathClosesExceptionChannel<&TestLoop::process>(0);
}

TEST(ExceptionTest, TaskDeathClosesProcessDebugExceptionChannel) {
  TaskDeathClosesExceptionChannel<&TestLoop::process>(ZX_EXCEPTION_CHANNEL_DEBUGGER);
}

TEST(ExceptionTest, TaskDeathClosesJobExceptionChannel) {
  TaskDeathClosesExceptionChannel<&TestLoop::job>(0);
}

TEST(ExceptionTest, TaskDeathClosesJobDebugExceptionChannel) {
  TaskDeathClosesExceptionChannel<&TestLoop::job>(ZX_EXCEPTION_CHANNEL_DEBUGGER);
}

TEST(ExceptionTest, ExceptionChannelOrder) {
  TestLoop loop;

  // Set the exception channels up in the expected order.
  zx::channel exception_channels[5];
  ASSERT_OK(loop.process().create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER,
                                                    &exception_channels[0]));
  ASSERT_OK(loop.aux_thread().create_exception_channel(0u, &exception_channels[1]));
  ASSERT_OK(loop.process().create_exception_channel(0u, &exception_channels[2]));
  ASSERT_OK(loop.job().create_exception_channel(0u, &exception_channels[3]));
  ASSERT_OK(loop.parent_job().create_exception_channel(0u, &exception_channels[4]));

  loop.CrashAuxThread();
  test_exceptions::ExceptionCatcher catcher(*zx::job::default_job());

  for (const zx::channel& channel : exception_channels) {
    ReadException(channel, ZX_EXCP_FATAL_PAGE_FAULT);
  }

  zx::status<zx::exception> result = catcher.ExpectException(loop.aux_thread());
  ASSERT_TRUE(result.is_ok());
  EXPECT_OK(loop.process().kill());
}

TEST(ExceptionTest, ExceptionChannelOrderWithSecondChanceDebugging) {
  TestLoop loop;

  // Set the exception channels up in their expected order, modulo that we
  // expect debugger to handle the exception again after the process exception
  // channel.
  zx::channel exception_channels[5];
  ASSERT_OK(loop.process().create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER,
                                                    &exception_channels[0]));
  ASSERT_OK(loop.aux_thread().create_exception_channel(0u, &exception_channels[1]));
  ASSERT_OK(loop.process().create_exception_channel(0u, &exception_channels[2]));
  ASSERT_OK(loop.job().create_exception_channel(0u, &exception_channels[3]));
  ASSERT_OK(loop.parent_job().create_exception_channel(0u, &exception_channels[4]));

  loop.CrashAuxThread();
  test_exceptions::ExceptionCatcher catcher(*zx::job::default_job());

  // First set the excpetion as 'second chance' and close its handle so it can
  // be tried by the next handler.
  {
    zx::exception exception = ReadException(exception_channels[0], ZX_EXCP_FATAL_PAGE_FAULT);
    SetExceptionStrategyProperty(exception, ZX_EXCEPTION_STRATEGY_SECOND_CHANCE);
  }
  int remaining_order[5] = {1, 2, 0, 3, 4};
  for (const int i : remaining_order) {
    ReadException(exception_channels[i], ZX_EXCP_FATAL_PAGE_FAULT);
  }

  zx::status<zx::exception> result = catcher.ExpectException(loop.aux_thread());
  ASSERT_TRUE(result.is_ok());
  EXPECT_OK(loop.process().kill());
}

TEST(ExceptionTest, DebugChannelClosedBeforeSecondChance) {
  // This case validates that a second chance exception with a closed debug
  // exception channel reverts to behaving like a first chance exception.

  TestLoop loop;

  // Set the exception channels up in their expected order, modulo that we
  // expect debugger to handle the exception again after the process exception
  // channel.
  zx::channel exception_channels[5];
  ASSERT_OK(loop.process().create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER,
                                                    &exception_channels[0]));
  ASSERT_OK(loop.aux_thread().create_exception_channel(0u, &exception_channels[1]));
  ASSERT_OK(loop.process().create_exception_channel(0u, &exception_channels[2]));
  ASSERT_OK(loop.job().create_exception_channel(0u, &exception_channels[3]));
  ASSERT_OK(loop.parent_job().create_exception_channel(0u, &exception_channels[4]));

  loop.CrashAuxThread();
  test_exceptions::ExceptionCatcher catcher(*zx::job::default_job());

  // We mark the exception as second chance, but then promptly close the
  // debugger exception channel.
  {
    zx::exception exception = ReadException(exception_channels[0], ZX_EXCP_FATAL_PAGE_FAULT);
    SetExceptionStrategyProperty(exception, ZX_EXCEPTION_STRATEGY_SECOND_CHANCE);
  }
  exception_channels[0].reset();

  int remaining_order[4] = {1, 2, 3, 4};
  for (const int i : remaining_order) {
    ReadException(exception_channels[i], ZX_EXCP_FATAL_PAGE_FAULT);
  }

  zx::status<zx::exception> result = catcher.ExpectException(loop.aux_thread());
  ASSERT_TRUE(result.is_ok());
  EXPECT_OK(loop.process().kill());
}

TEST(ExceptionTest, ThreadLifecycleChannelExceptions) {
  TestLoop loop(TestLoop::Control::kManual);

  loop.Step1CreateProcess();
  zx::channel exception_channel;
  ASSERT_OK(
      loop.process().create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER, &exception_channel));

  // We should get both primary and aux thread exceptions.
  loop.Step2StartThreads();

  zx_exception_info_t primary_start_info;
  {
    zx::exception exception =
        ReadException(exception_channel, ZX_EXCP_THREAD_STARTING, &primary_start_info);
    zx_info_handle_basic_t basic_info;
    zx_status_t status = loop.process().get_info(ZX_INFO_HANDLE_BASIC, &basic_info,
                                                 sizeof(basic_info), nullptr, nullptr);
    ASSERT_EQ(status, ZX_OK);
    zx_koid_t process_koid = basic_info.koid;
    EXPECT_EQ(primary_start_info.pid, process_koid);
    EXPECT_TRUE(ExceptionHasThread(exception, primary_start_info.tid));
    EXPECT_TRUE(ExceptionHasProcess(exception, primary_start_info.pid));
  }

  zx_exception_info_t aux_start_info;
  {
    zx::exception exception =
        ReadException(exception_channel, ZX_EXCP_THREAD_STARTING, &aux_start_info);
    zx_info_handle_basic_t basic_info;
    zx_status_t status = loop.process().get_info(ZX_INFO_HANDLE_BASIC, &basic_info,
                                                 sizeof(basic_info), nullptr, nullptr);
    ASSERT_EQ(status, ZX_OK);
    zx_koid_t process_koid = basic_info.koid;
    EXPECT_EQ(aux_start_info.pid, process_koid);
    EXPECT_TRUE(ExceptionHasThread(exception, aux_start_info.tid));
    EXPECT_TRUE(ExceptionHasProcess(exception, aux_start_info.pid));
  }

  // We don't have access to the primary thread handle so just check the aux
  // thread TID to make sure it's correct.
  loop.Step3ReadAuxThreadHandle();
  zx_info_handle_basic_t basic_info;
  zx_status_t status = loop.aux_thread().get_info(ZX_INFO_HANDLE_BASIC, &basic_info,
                                                  sizeof(basic_info), nullptr, nullptr);
  ASSERT_EQ(status, ZX_OK);
  zx_koid_t aux_thread_koid = basic_info.koid;
  EXPECT_EQ(aux_start_info.tid, aux_thread_koid);

  loop.Step4ShutdownAuxThread();
  zx_exception_info_t aux_exit_info;
  {
    zx::exception exception =
        ReadException(exception_channel, ZX_EXCP_THREAD_EXITING, &aux_exit_info);
    EXPECT_TRUE(ExceptionHasThread(exception, aux_exit_info.tid));
    EXPECT_TRUE(ExceptionHasProcess(exception, aux_exit_info.pid));
    EXPECT_EQ(aux_exit_info.tid, aux_start_info.tid);
    EXPECT_EQ(aux_exit_info.pid, aux_start_info.pid);
  }

  loop.Step5ShutdownMainThread();
  zx_exception_info_t primary_exit_info;
  {
    zx::exception exception =
        ReadException(exception_channel, ZX_EXCP_THREAD_EXITING, &primary_exit_info);
    EXPECT_TRUE(ExceptionHasThread(exception, primary_exit_info.tid));
    EXPECT_TRUE(ExceptionHasProcess(exception, primary_exit_info.pid));
    EXPECT_EQ(primary_exit_info.tid, primary_start_info.tid);
    EXPECT_EQ(primary_exit_info.pid, primary_start_info.pid);
  }
}

// Parameterized to run against either the TestLoop job or parent job.
template <auto task_func>
void VerifyProcessLifecycle() {
  zx::channel exception_channel;
  {
    TestLoop loop(TestLoop::Control::kManual);

    ASSERT_OK((loop.*task_func)().create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER,
                                                           &exception_channel));

    // ZX_EXCP_PROCESS_STARTING shouldn't be sent until step 2 when we
    // actually start the first thread on the process.
    loop.Step1CreateProcess();
    EXPECT_EQ(
        exception_channel.wait_one(ZX_CHANNEL_READABLE, zx::deadline_after(kTestTimeout), nullptr),
        ZX_ERR_TIMED_OUT);

    loop.Step2StartThreads();
    zx_exception_info_t info;
    {
      zx::exception exception = ReadException(exception_channel, ZX_EXCP_PROCESS_STARTING, &info);
      zx_info_handle_basic_t basic_info;
      zx_status_t status = loop.process().get_info(ZX_INFO_HANDLE_BASIC, &basic_info,
                                                   sizeof(basic_info), nullptr, nullptr);
      ASSERT_EQ(status, ZX_OK);
      EXPECT_EQ(info.pid, basic_info.koid);
      EXPECT_TRUE(ExceptionHasThread(exception, info.tid));
      EXPECT_TRUE(ExceptionHasProcess(exception, info.pid));
    }

    loop.Step3ReadAuxThreadHandle();
    loop.Step4ShutdownAuxThread();
    loop.Step5ShutdownMainThread();
  }

  // There is no PROCESS_EXITING exception, make sure the kernel finishes
  // closing the channel without putting anything else in it.
  //
  // Unlike processes, jobs don't automatically die with their last child,
  // so the TestLoop handles must be fully closed at this point to get the
  // PEER_CLOSED signal.
  zx_signals_t signals;
  EXPECT_OK(exception_channel.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &signals));
  EXPECT_FALSE(signals & ZX_CHANNEL_READABLE);
}

TEST(ExceptionTest, ProcessLifecycleJobChannel) { VerifyProcessLifecycle<&TestLoop::job>(); }

TEST(ExceptionTest, ProcessLifecycleParentJobChannel) {
  VerifyProcessLifecycle<&TestLoop::parent_job>();
}

TEST(ExceptionTest, ProcessStartExceptionDoesNotBubbleUp) {
  zx::channel parent_exception_channel;
  zx::channel exception_channel;
  {
    TestLoop loop(TestLoop::Control::kManual);

    ASSERT_OK(loop.parent_job().create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER,
                                                         &parent_exception_channel));
    ASSERT_OK(
        loop.job().create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER, &exception_channel));

    loop.Step1CreateProcess();
    loop.Step2StartThreads();
    ReadException(exception_channel, ZX_EXCP_PROCESS_STARTING);

    loop.Step3ReadAuxThreadHandle();
    loop.Step4ShutdownAuxThread();
    loop.Step5ShutdownMainThread();
  }

  // The parent job channel should never have seen anything since synthetic
  // PROCESS_STARTING exceptions do not bubble up the job chain.
  zx_signals_t signals;
  EXPECT_OK(
      parent_exception_channel.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &signals));
  EXPECT_FALSE(signals & ZX_CHANNEL_READABLE);
}

// Lifecycle exceptions should not be seen by normal (non-debug) handlers.
TEST(ExceptionTest, LifecycleExceptionsToDebugHandlersOnly) {
  zx::channel exception_channels[4];
  {
    TestLoop loop(TestLoop::Control::kManual);
    ASSERT_OK(loop.parent_job().create_exception_channel(0, &exception_channels[0]));
    ASSERT_OK(loop.job().create_exception_channel(0, &exception_channels[1]));

    loop.Step1CreateProcess();
    ASSERT_OK(loop.process().create_exception_channel(0, &exception_channels[2]));

    loop.Step2StartThreads();
    loop.Step3ReadAuxThreadHandle();
    ASSERT_OK(loop.aux_thread().create_exception_channel(0, &exception_channels[3]));

    loop.Step4ShutdownAuxThread();
    loop.Step5ShutdownMainThread();
  }

  // None of the normal handlers should have seen any exceptions.
  for (const zx::channel& channel : exception_channels) {
    zx_signals_t signals;
    EXPECT_OK(channel.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &signals));
    EXPECT_FALSE(signals & ZX_CHANNEL_READABLE);
  }
}

// Returns the state of the thread underlying the given exception or
// an invalid state on failure.
zx_thread_state_t GetExceptionThreadState(const zx::exception& exception) {
  zx::thread thread;
  if (exception.get_thread(&thread) != ZX_OK) {
    return ~0;
  }
  zx_info_thread_t info;
  zx_status_t status = thread.get_info(ZX_INFO_THREAD, &info, sizeof(info), nullptr, nullptr);
  ZX_ASSERT(status == ZX_OK);
  return info.state;
}

// A lifecycle exception blocks due to:
//   * process/thread start
//   * thread killing itself via zx_thread_exit()
//
// It does not block due to:
//   * zx_task_kill() on the thread or any of its parents
//
// In the non-blocking case, the exception is still sent, but the thread
// doesn't wait for a response.
TEST(ExceptionTest, LifecycleBlocking) {
  TestLoop loop(TestLoop::Control::kManual);
  loop.Step1CreateProcess();

  zx::channel job_channel;
  ASSERT_OK(loop.job().create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER, &job_channel));
  zx::channel process_channel;
  ASSERT_OK(
      loop.process().create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER, &process_channel));

  // Process/thread start: exception handler should block the task.
  loop.Step2StartThreads();
  {
    zx::exception exception = ReadException(job_channel, ZX_EXCP_PROCESS_STARTING);
    zx::nanosleep(zx::deadline_after(kTestTimeout));
    EXPECT_EQ(GetExceptionThreadState(exception), ZX_THREAD_STATE_BLOCKED_EXCEPTION);
  }
  for (int i = 0; i < 2; ++i) {
    zx::exception exception = ReadException(process_channel, ZX_EXCP_THREAD_STARTING);
    zx::nanosleep(zx::deadline_after(kTestTimeout));
    EXPECT_EQ(GetExceptionThreadState(exception), ZX_THREAD_STATE_BLOCKED_EXCEPTION);
  }

  // The aux thread exits gracefully via zx_thread_exit() so should block.
  loop.Step3ReadAuxThreadHandle();
  loop.Step4ShutdownAuxThread();
  {
    zx::exception exception = ReadException(process_channel, ZX_EXCP_THREAD_EXITING);
    zx::nanosleep(zx::deadline_after(kTestTimeout));
    // The thread reports DYING because it takes precedence over BLOCKED,
    // but if it wasn't actually blocking it would report DEAD by now.
    EXPECT_EQ(GetExceptionThreadState(exception), ZX_THREAD_STATE_DYING);
  }

  // The main thread shuts down the whole process via zx_task_kill() so
  // should not block.
  loop.Step5ShutdownMainThread();
  {
    zx::exception exception = ReadException(process_channel, ZX_EXCP_THREAD_EXITING);
    zx::thread thread;
    EXPECT_OK(zx_exception_get_thread(exception.get(), thread.reset_and_get_address()));
    EXPECT_OK(thread.wait_one(ZX_THREAD_TERMINATED, zx::time::infinite(), nullptr));
    EXPECT_EQ(GetExceptionThreadState(exception), ZX_THREAD_STATE_DEAD);
  }
}

// Test read/write register state during (non-synthetic) exceptions.
//
// |task_func|: TestLoop member function to get the task.
// |create_flags|: flags to pass to zx_task_create_exception_channel().
template <auto task_func>
void ReadWriteThreadState(uint32_t create_flags) {
  TestLoop loop;
  zx::channel exception_channel;
  ASSERT_OK((loop.*task_func)().create_exception_channel(create_flags, &exception_channel));

  loop.CrashAuxThread();
  zx::exception exception = ReadException(exception_channel, ZX_EXCP_FATAL_PAGE_FAULT);

  zx_thread_state_general_regs_t regs;
  EXPECT_OK(loop.aux_thread().read_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)));
  EXPECT_OK(loop.aux_thread().write_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)));

  EXPECT_OK(loop.process().kill());
}

TEST(ExceptionTest, ReadWriteThreadStateFromThreadChannel) {
  ReadWriteThreadState<&TestLoop::aux_thread>(0);
}

TEST(ExceptionTest, ReadWriteThreadStateFromProcessChannel) {
  ReadWriteThreadState<&TestLoop::process>(0);
}

TEST(ExceptionTest, ReadWriteThreadStateFromProcessDebugChannel) {
  ReadWriteThreadState<&TestLoop::process>(ZX_EXCEPTION_CHANNEL_DEBUGGER);
}

TEST(ExceptionTest, ReadWriteThreadStateFromJobChannel) { ReadWriteThreadState<&TestLoop::job>(0); }

TEST(ExceptionTest, ReadWriteThreadStateFromParentJobChannel) {
  ReadWriteThreadState<&TestLoop::parent_job>(0);
}

// Processes an exception and returns the result of trying to read/write
// the thread general registers.
//
// If read/write return different status, marks a test failure and returns
// ZX_ERR_INTERNAL.
zx_status_t ExceptionRegAccess(const zx::channel& channel, zx_excp_type_t type) {
  zx_exception_info_t info;
  zx::exception exception = ReadException(channel, type, &info);

  zx::thread thread;
  EXPECT_OK(exception.get_thread(&thread));
  if (!thread.is_valid()) {
    return ZX_ERR_INTERNAL;
  }

  zx_thread_state_general_regs_t regs;
  zx_status_t read_status = thread.read_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs));
  zx_status_t write_status = thread.write_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs));

  EXPECT_EQ(read_status, write_status);
  if (read_status != write_status) {
    return ZX_ERR_INTERNAL;
  }
  return read_status;
}

// Read/write register state is supported during STARTING exceptions, but not
// during EXITING.
TEST(ExceptionTest, SyntheticExceptionReadWriteRegs) {
  zx::channel job_channel;
  zx::channel process_channel;

  TestLoop loop(TestLoop::Control::kManual);
  ASSERT_OK(loop.job().create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER, &job_channel));

  loop.Step1CreateProcess();
  ASSERT_OK(
      loop.process().create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER, &process_channel));

  loop.Step2StartThreads();
  EXPECT_OK(ExceptionRegAccess(job_channel, ZX_EXCP_PROCESS_STARTING));
  EXPECT_OK(ExceptionRegAccess(process_channel, ZX_EXCP_THREAD_STARTING));
  EXPECT_OK(ExceptionRegAccess(process_channel, ZX_EXCP_THREAD_STARTING));

  loop.Step3ReadAuxThreadHandle();
  loop.Step4ShutdownAuxThread();
  EXPECT_EQ(ExceptionRegAccess(process_channel, ZX_EXCP_THREAD_EXITING), ZX_ERR_NOT_SUPPORTED);

  // When the main thread is shut down it kills the whole process, which
  // causes it to stop waiting for responses from exception handlers. We'll
  // still receive the exception, but by the time we process it here it's
  // likely that the thread is already dead so we can't check reg access.
  loop.Step5ShutdownMainThread();
  ReadException(process_channel, ZX_EXCP_THREAD_EXITING);
}

static const char* check_trigger(int argc, char** argv) {
  static const char trigger[] = "trigger=";
  for (int i = 1; i < argc; ++i) {
    if (strncmp(argv[i], trigger, sizeof(trigger) - 1) == 0) {
      return argv[i] + sizeof(trigger) - 1;
    }
  }
  return NULL;
}

}  // namespace

int main(int argc, char** argv) {
  program_path = argv[0];

  // We use this same binary for both the main test runner and a test process
  // running msg_loop(), but this can interfere with any common zxtest
  // arguments that get passed. If this becomes a problem, consider using
  // mini-process as the test process instead.
  if (argc >= 2) {
    const char* excp_name = check_trigger(argc, argv);
    if (excp_name) {
      test_child_trigger(excp_name);
      return 0;
    }
    if (strcmp(argv[1], test_child_name) == 0) {
      test_child();
      return 0;
    }
    if (strcmp(argv[1], exit_closing_excp_handle_child_name) == 0) {
      test_child_exit_closing_excp_handle();
      /* NOTREACHED */
    }
  }

  return RUN_ALL_TESTS(argc, argv);
}
