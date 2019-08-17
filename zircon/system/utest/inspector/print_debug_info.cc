// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/backtrace-request/backtrace-request.h>
#include <lib/zx/event.h>
#include <lib/zx/exception.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <stdio.h>
#include <string.h>
#include <zircon/syscalls/exception.h>
#include <zircon/threads.h>

#include <memory>

#include <inspector/inspector.h>
#include <zxtest/zxtest.h>

namespace {

// Test utilities ----------------------------------------------------------------------------------

struct ThreadContext {
  zx::unowned<zx::thread> thread;
  zx::channel exception_channel;

  // NOTE: Not all events are used by all tests.
  zx::event crash_thread_ready;  // Set when the crash thread is about to crash.
  zx::event test_done;           // Set when test is OK and other threads should exit.
};

ThreadContext SetupThreadContext() {
  ThreadContext context;
  zx::event::create(0, &context.crash_thread_ready);
  zx::event::create(0, &context.test_done);
  return context;
}

struct ExceptionReport {
  zx::exception exception;
  zx_exception_info_t info;
  zx_thread_state_general_regs_t regs;
};

ExceptionReport WaitForException(ThreadContext* context) {
  ExceptionReport report;

  // We wait for the exception.
  context->exception_channel.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), nullptr);
  context->exception_channel.read(0, &report.info, report.exception.reset_and_get_address(),
                                  sizeof(zx_exception_info_t), 1, nullptr, nullptr);

  context->thread->read_state(ZX_THREAD_STATE_GENERAL_REGS, &report.regs, sizeof(report.regs));

  return report;
}

std::string GetProcessName() {
  // Search for the process name.
  char process_name[ZX_MAX_NAME_LEN];
  zx::process::self()->get_property(ZX_PROP_NAME, process_name, sizeof(process_name));
  return process_name;
}

void ResumeException(ThreadContext* context, ExceptionReport* report) {
#if defined(__aarch64__)
  // Skip past the brk instruction. Otherwise the breakpoint will trigger again.
  report->regs.pc += 4;
  ASSERT_OK(context->thread->write_state(ZX_THREAD_STATE_GENERAL_REGS, &report->regs,
                                         sizeof(report->regs)));
#endif

  uint32_t handled = ZX_EXCEPTION_STATE_HANDLED;
  ASSERT_OK(report->exception.set_property(ZX_PROP_EXCEPTION_STATE, &handled, sizeof(handled)));
  report->exception.reset();
}

// Thread Functions --------------------------------------------------------------------------------

int CrashThreadFunction(void* user) {
  ThreadContext* context = reinterpret_cast<ThreadContext*>(user);

  zx_status_t status;
  // Bind the exception channel for the caller.
  status = zx::thread::self()->create_exception_channel(0, &context->exception_channel);
  if (status != ZX_OK) {
    printf("Could not get exception channel: %s.\n", zx_status_get_string(status));
    return 1;
  }

  // Let them know we're ready.
  status = context->crash_thread_ready.signal(0, ZX_USER_SIGNAL_0);
  if (status != ZX_OK) {
    printf("Could not signal thread ready: %s.\n", zx_status_get_string(status));
    return 1;
  }

  // Throw a SW breakpoint exception.
  backtrace_request();

  return 0;
}

int LoopThread(void* user) {
  ThreadContext* context = reinterpret_cast<ThreadContext*>(user);
  context->test_done.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), 0);
  return 0;
}

}  // namespace

// Tests -------------------------------------------------------------------------------------------

TEST(Inspector, PrintDebugInfoForOneThread) {
  constexpr char kThreadName[] = "main-test-thread";
  ThreadContext context = SetupThreadContext();

  thrd_t c_thread;
  ASSERT_EQ(thrd_create_with_name(&c_thread, CrashThreadFunction, &context, kThreadName), 0);
  context.thread = zx::unowned<zx::thread>(thrd_get_zx_handle(c_thread));

  // Wait for the thread to tell us we're ready.
  ASSERT_OK(context.crash_thread_ready.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr));

  ExceptionReport report = WaitForException(&context);

  // Create a temporary file to hold the output of the inspector call.
  constexpr size_t kBufSize = 1024 * 1024;
  auto buf = std::make_unique<char[]>(kBufSize + 1);
  buf[kBufSize] = '\0';
  FILE* inspect_out = fmemopen(buf.get(), kBufSize, "r+");
  ASSERT_NE(inspect_out, nullptr);

  inspector_print_debug_info(inspect_out, zx_process_self(), thrd_get_zx_handle(c_thread));
  fclose(inspect_out);

  std::string inspector_output(buf.get());
  ASSERT_FALSE(inspector_output.empty());

  ASSERT_NE(inspector_output.find(GetProcessName()), std::string::npos);
  ASSERT_NE(inspector_output.find(kThreadName), std::string::npos);
  ASSERT_NE(inspector_output.find("sw breakpoint"), std::string::npos);

  ResumeException(&context, &report);

  // Join the exception thread.
  int res = -1;
  ASSERT_EQ(thrd_join(c_thread, &res), thrd_success);
  ASSERT_EQ(res, 0);
}

TEST(Inspector, PrintDebugInfoForManyThreads) {
  ThreadContext context = SetupThreadContext();

  // Create threads that will loop until the signal is off.
  constexpr int kLoopThreadCount = 5;
  thrd_t loop_threads[kLoopThreadCount];
  std::string loop_thread_names[kLoopThreadCount];

  for (int i = 0; i < kLoopThreadCount; i++) {
    char buf[128];
    snprintf(buf, sizeof(buf), "loop_thread_%d", i);
    ASSERT_EQ(thrd_create_with_name(loop_threads + i, LoopThread, &context, buf), 0);
    loop_thread_names[i] = buf;
  }

  // Create the main crash thread.
  constexpr char kThreadName[] = "main-test-thread";
  thrd_t c_thread;
  ASSERT_EQ(thrd_create_with_name(&c_thread, CrashThreadFunction, &context, kThreadName), 0);
  context.thread = zx::unowned<zx::thread>(thrd_get_zx_handle(c_thread));

  // Wait for the thread to tell us we're ready.
  ASSERT_OK(context.crash_thread_ready.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr));

  ExceptionReport report = WaitForException(&context);

  // Create a temporary file to hold the output of the inspector call.
  constexpr size_t kBufSize = 1024 * 1024;
  auto buf = std::make_unique<char[]>(kBufSize + 1);
  buf[kBufSize] = '\0';
  FILE* inspect_out = fmemopen(buf.get(), kBufSize, "r+");
  ASSERT_NE(inspect_out, nullptr);

  inspector_print_debug_info_for_all_threads(inspect_out, zx_process_self());
  fclose(inspect_out);

  std::string inspector_output(buf.get());
  ASSERT_FALSE(inspector_output.empty());

  // Search for the main thread. It should appear first.
  ASSERT_NE(inspector_output.find(GetProcessName()), std::string::npos);

  size_t pos = std::string::npos;
  pos = inspector_output.find(kThreadName);
  ASSERT_NE(pos, std::string::npos);
  ASSERT_EQ(inspector_output.find(kThreadName, pos + 1), std::string::npos);

  // Each name should only appear once.
  for (int i = 0; i < kLoopThreadCount; i++) {
    pos = inspector_output.find(loop_thread_names[i]);
    ASSERT_NE(pos, std::string::npos, "%s not found.", loop_thread_names[i].c_str());
    ASSERT_EQ(inspector_output.find(loop_thread_names[i], pos + 1), std::string::npos,
              "%s found twice.", loop_thread_names[i].c_str());
  }

  // Exception should only appear once.
  pos = inspector_output.find("sw breakpoint");
  ASSERT_NE(pos, std::string::npos);
  ASSERT_EQ(inspector_output.find("sw breakpoint", pos + 1), std::string::npos);

  ResumeException(&context, &report);

  // Join the exception thread.
  int res = -1;
  ASSERT_EQ(thrd_join(c_thread, &res), thrd_success);
  ASSERT_EQ(res, 0);

  // Tell the loop threads we're done.
  context.test_done.signal(0, ZX_USER_SIGNAL_0);
  for (int i = 0; i < kLoopThreadCount; i++) {
    res = -1;
    ASSERT_EQ(thrd_join(loop_threads[i], &res), thrd_success);
    ASSERT_EQ(res, 0);
  }
}

// Run tests ---------------------------------------------------------------------------------------

int main(int argc, char* argv[]) { RUN_ALL_TESTS(argc, argv); }
