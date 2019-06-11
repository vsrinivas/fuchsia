// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _ALL_SOURCE
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include <mutex>
#include <string>
#include <vector>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

// This binary is meant to be a playground for testing different multi-threading
// behaviour/signaling/border cases.
//
// No code should depends on this, but rather is meant to be a sandox for
// zxdb developers.

namespace {

constexpr int kThreadCount = 5;

struct ThreadContext {
  int index = 0;
  std::string name;
  thrd_t handle;
  zx_handle_t zx_handle;
};

const char* ThreadStateToString(uint32_t state) {
  switch (state) {
    case ZX_THREAD_STATE_NEW:
      return "ZX_THREAD_STATE_NEW";
    case ZX_THREAD_STATE_RUNNING:
      return "ZX_THREAD_STATE_RUNNING";
    case ZX_THREAD_STATE_SUSPENDED:
      return "ZX_THREAD_STATE_SUSPENDED";
    case ZX_THREAD_STATE_BLOCKED:
      return "ZX_THREAD_STATE_BLOCKED";
    case ZX_THREAD_STATE_DYING:
      return "ZX_THREAD_STATE_DYING";
    case ZX_THREAD_STATE_DEAD:
      return "ZX_THREAD_STATE_DEAD";
    case ZX_THREAD_STATE_BLOCKED_EXCEPTION:
      return "ZX_THREAD_STATE_BLOCKED_EXCEPTION";
    case ZX_THREAD_STATE_BLOCKED_SLEEPING:
      return "ZX_THREAD_STATE_BLOCKED_SLEEPING";
    case ZX_THREAD_STATE_BLOCKED_FUTEX:
      return "ZX_THREAD_STATE_BLOCKED_FUTEX";
    case ZX_THREAD_STATE_BLOCKED_PORT:
      return "ZX_THREAD_STATE_BLOCKED_PORT";
    case ZX_THREAD_STATE_BLOCKED_CHANNEL:
      return "ZX_THREAD_STATE_BLOCKED_CHANNEL";
    case ZX_THREAD_STATE_BLOCKED_WAIT_ONE:
      return "ZX_THREAD_STATE_BLOCKED_WAIT_ONE";
    case ZX_THREAD_STATE_BLOCKED_WAIT_MANY:
      return "ZX_THREAD_STATE_BLOCKED_WAIT_MANY";
    case ZX_THREAD_STATE_BLOCKED_INTERRUPT:
      return "ZX_THREAD_STATE_BLOCKED_INTERRUPT";
    case ZX_THREAD_STATE_BLOCKED_PAGER:
      return "ZX_THREAD_STATE_BLOCKED_PAGER";
    default:
      break;
  }

  return "<unknown>";
}

std::mutex kMutex;

#define PRINT(...)     \
  printf(__VA_ARGS__); \
  fflush(stdout)

void __NO_INLINE PrintFunction(ThreadContext* ctx, int i) {
  PRINT("%s: message %d\n", ctx->name.c_str(), i);
};

int __NO_INLINE ThreadFunction(void* in) {
  ThreadContext* ctx = reinterpret_cast<ThreadContext*>(in);
  for (int i = 0; i < 50; i++) {
    {
      std::lock_guard<std::mutex> lock(kMutex);
      PrintFunction(ctx, i);
    }
    zx_nanosleep(zx_deadline_after(ZX_MSEC(500 * (ctx->index + 1))));
  }

  return 0;
}

std::unique_ptr<ThreadContext> CreateThread(const std::string& name,
                                            thrd_start_t func,
                                            void* ctx = nullptr) {
  auto context = std::make_unique<ThreadContext>();
  context->name = name;
  thrd_create_with_name(&context->handle, func, ctx ? ctx : context.get(),
                        context->name.c_str());
  context->zx_handle = thrd_get_zx_handle(context->handle);
  return context;
}

std::vector<std::unique_ptr<ThreadContext>> CreateThreads(int count) {
  std::vector<std::unique_ptr<ThreadContext>> contexts;
  contexts.reserve(count);

  for (int i = 0; i < kThreadCount; i++) {
    auto context = std::make_unique<ThreadContext>();

    context->index = i;
    context->name = fxl::StringPrintf("thread-%d", i);
    thrd_create_with_name(&context->handle, ThreadFunction, context.get(),
                          context->name.c_str());

    context->zx_handle = thrd_get_zx_handle(context->handle);
    contexts.push_back(std::move(context));
  }

  return contexts;
}

// Printing --------------------------------------------------------------------

// Simple application that prints from several threads.
void MultithreadedPrinting() {
  auto contexts = CreateThreads(kThreadCount);

  for (auto& context : contexts) {
    int res = 0;
    thrd_join(context->handle, &res);
  }
}

// Suspending ------------------------------------------------------------------

void Suspending() {
  auto contexts = CreateThreads(kThreadCount);

  PRINT("Suspending all the threads.\n");
  for (int i = 0; i < kThreadCount; i++) {
    zx_handle_t suspend_token;
    zx_status_t status =
        zx_task_suspend(contexts[i]->zx_handle, &suspend_token);
    if (status != ZX_OK) {
      PRINT("Could not suspend thread %d: %s\n", i,
            zx_status_get_string(status));
      exit(1);
    }
  }

  PRINT("Waiting for suspend notifications.\n");
  for (int i = 0; i < kThreadCount; i++) {
    zx_signals_t signals;
    zx_status_t status =
        zx_object_wait_one(contexts[i]->zx_handle, ZX_THREAD_SUSPENDED,
                           zx_deadline_after(ZX_MSEC(100)), &signals);
    if (status != ZX_OK) {
      PRINT("Could not wait for signal for thread %d: %s\n", i,
            zx_status_get_string(status));
      exit(1);
    }

    if ((signals & ZX_THREAD_SUSPENDED) == 0) {
      PRINT("Did not get suspended signal for thread %d: %d\n", i, signals);
      exit(1);
    }

    PRINT("Successfully suspended thread %i\n", i);
  }
}

// Wait State ------------------------------------------------------------------

std::atomic<bool> gEntered = false;
std::atomic<bool> gExit = false;

int InfiniteFunction(void*) {
  while (!gExit) {
    gEntered = true;
    zx_nanosleep(zx_deadline_after(ZX_MSEC(500)));
  }

  return 0;
}

std::atomic<bool> gSecondStarted = false;

int WaitOnThread(void* ctx) {
  gSecondStarted = true;
  ThreadContext* first_thread = reinterpret_cast<ThreadContext*>(ctx);
  int res = 1;
  thrd_join(first_thread->handle, &res);
  FXL_DCHECK(res == 0);
  return 0;
}

zx_info_thread GetThreadState(zx_handle_t thread_handle) {
  zx_info_thread info;
  zx_status_t status = zx_object_get_info(thread_handle, ZX_INFO_THREAD, &info,
                                          sizeof(info), nullptr, nullptr);
  FXL_DCHECK(status == ZX_OK) << "Got: " << zx_status_get_string(status);

  return info;
}

void WaitState() {
  auto first_thread = CreateThread("infinite", InfiniteFunction);

  while (!gEntered)
    zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));

  FXL_LOG(INFO) << "Thread entered infinite loop.";

  auto second_thread =
      CreateThread("wait-on-first", WaitOnThread, first_thread.get());
  while (!gSecondStarted)
    zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));

  FXL_LOG(INFO) << "Created second thread.";

  // Print the state of the second thread.

  for (int i = 0; i < 10; i++) {
    auto info = GetThreadState(second_thread->zx_handle);
    FXL_LOG(INFO) << "Got status: " << ThreadStateToString(info.state);
    if (info.state == ZX_THREAD_STATE_BLOCKED_FUTEX)
      break;

    zx_nanosleep(zx_deadline_after(ZX_SEC(1)));
  }

  {
    // Suspend the thread.
    zx_handle_t suspend_token;
    zx_status_t status =
        zx_task_suspend(second_thread->zx_handle, &suspend_token);
    FXL_DCHECK(status == ZX_OK) << "Got: " << zx_status_get_string(status);

    // Wait for signal.
    zx_signals_t observed;
    status = zx_object_wait_one(second_thread->zx_handle, ZX_THREAD_SUSPENDED,
                                zx_deadline_after(ZX_SEC(1)), &observed);
    FXL_DCHECK(status == ZX_OK) << "Got: " << zx_status_get_string(status);
    FXL_DCHECK((observed & ZX_THREAD_SUSPENDED) != 0);

    auto info = GetThreadState(second_thread->zx_handle);
    FXL_LOG(INFO) << "Got status: " << ThreadStateToString(info.state);

    zx_handle_close(suspend_token);
  }

  FXL_LOG(INFO) << "Exiting.";
  gExit = true;
  int res = 2000;
  FXL_DCHECK(thrd_join(second_thread->handle, &res) == thrd_success);
  FXL_DCHECK(res == 0) << "Res: " << res;
  FXL_DCHECK(thrd_join(first_thread->handle, &res) == thrd_success);
  FXL_DCHECK(res == 0) << "Res: " << res;
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc == 1 || strcmp(argv[1], "printing") == 0) {
    MultithreadedPrinting();
  } else if (strcmp(argv[1], "suspending") == 0) {
    Suspending();
  } else if (strcmp(argv[1], "wait_state") == 0) {
    WaitState();
  } else {
    PRINT("Unknown option: %s\n", argv[1]);
    return 1;
  }

  return 0;
}
