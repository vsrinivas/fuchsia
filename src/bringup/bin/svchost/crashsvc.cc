// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/backtrace-request/backtrace-request-utils.h>
#include <lib/zx/channel.h>
#include <lib/zx/exception.h>
#include <lib/zx/handle.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <threads.h>
#include <zircon/status.h>
#include <zircon/syscalls/exception.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include <memory>

#include <crashsvc/crashsvc.h>
#include <crashsvc/exception_handler.h>
#include <crashsvc/logging.h>
#include <inspector/inspector.h>

#include "src/lib/fsl/handles/object_info.h"

namespace {

struct crash_ctx {
  zx::channel exception_channel;
  zx_handle_t exception_handler_svc;
};

// Cleans up and resumes a thread in a manual backtrace request.
//
// This may modify |regs| via cleanup_backtrace_request().
//
// Returns true and sets |exception| to resume on close on success.
bool ResumeIfBacktraceRequest(const zx::thread& thread, const zx::exception& exception,
                              const zx_exception_info& info, zx_thread_state_general_regs_t* regs) {
  if (is_backtrace_request(info.type, regs)) {
    if (const zx_status_t status = cleanup_backtrace_request(thread.get(), regs); status != ZX_OK) {
      LogError("failed to cleanup backtrace", info, status);
      return false;
    }

    // Mark the exception as handled so the thread resumes execution.
    uint32_t state = ZX_EXCEPTION_STATE_HANDLED;
    if (const zx_status_t status =
            exception.set_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state));
        status != ZX_OK) {
      LogError("failed to resume from backtrace", info, status);
      return false;
    }

    return true;
  }

  return false;
}

void HandOffException(zx::exception exception, const zx_exception_info_t& info,
                      ExceptionHandler& handler, async::Loop& loop) {
  zx::process process;
  if (const zx_status_t status = exception.get_process(&process); status != ZX_OK) {
    LogError("failed to get exception process when receiving exception", info, status);
    return;
  }

  zx::thread thread;
  if (const zx_status_t status = exception.get_thread(&thread); status != ZX_OK) {
    LogError("failed to get exception thread when receiving exception", info, status);
    return;
  }

  // A backtrace request should just dump and continue.
  zx_thread_state_general_regs_t regs;
  if (const zx_status_t status = inspector_read_general_regs(thread.get(), &regs);
      status != ZX_OK) {
    LogError("failed to get general registers", info, status);
  }

  // If this is a backtrace request, we print all the the threads and then return.
  if (ResumeIfBacktraceRequest(thread, exception, info, &regs)) {
    inspector_print_debug_info_for_all_threads(stdout, process.get());
    return;
  }

  // Dump the crash info to the logs whether we have a FIDL handler or not.
  fprintf(stdout, "crashsvc: exception received, processing\n");
  inspector_print_debug_info(stdout, process.get(), thread.get());

  // If the process serving fuchsia.exception.Handler crashes, the system will still send future
  // fuchsia.exception.Handler/OnException requests to that process as it is still alive and
  // therefore the exception will be stuck in the underlying channel, never terminating the process.
  // So we release the exception here to terminate the process and unfortunately forgo further
  // exception handling for that exception.
  //
  // This needs to be kept in sync with the name of the process serving
  // fuchsia.exception.Handler.
  if (fsl::GetObjectName(process.get()) == "exceptions.cmx") {
    LogError("cannot handle exception for the process serving fuchsia.exception.Handler",
             ZX_ERR_NOT_SUPPORTED);

    // Release the exception to let the kernel terminate the process.
    exception.reset();
    return;
  }

  // Send over the exception to the handler.
  // From this point on, crashsvc has no ownership over the exception and it's up to the handler to
  // decide when and how to resume it.
  //
  // This is done asynchronously to give queued tasks, like the reconnection logic, a chance to
  // execute.
  async::PostTask(loop.dispatcher(), [&handler, info, exception = std::move(exception)]() mutable {
    handler.Handle(std::move(exception), info);
  });
}

int crash_svc(void* arg) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto ctx = std::unique_ptr<crash_ctx>(reinterpret_cast<crash_ctx*>(arg));
  ExceptionHandler handler(loop.dispatcher(), ctx->exception_handler_svc);
  async::Wait wait_for_exceptions(ctx->exception_channel.get(),
                                  ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, /*options=*/0u);

  wait_for_exceptions.set_handler([&loop, &handler, &ctx](async_dispatcher_t* dispatcher,
                                                          async::Wait* wait, zx_status_t status,
                                                          const zx_packet_signal_t* signal) {
    if (status == ZX_ERR_CANCELED) {
      loop.Shutdown();
      return;
    }

    if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
      // We should only get here in crashsvc's unit tests. In production, our job is actually the
      // root job so the system will halt before closing its exception channel.
      loop.Shutdown();
      return;
    }

    zx_exception_info_t info;
    zx::exception exception;
    if (const zx_status_t status = ctx->exception_channel.read(
            0, &info, exception.reset_and_get_address(), sizeof(info), 1, nullptr, nullptr);
        status != ZX_OK) {
      LogError("failed to read from the exception channel", status);
      return;
    }

    HandOffException(std::move(exception), info, handler, loop);

    if (const zx_status_t status = wait->Begin(loop.dispatcher()); status != ZX_OK) {
      LogError("Failed to restart wait, crashsvc won't continue", status);
      loop.Shutdown();
      return;
    }
  });

  if (const zx_status_t status = wait_for_exceptions.Begin(loop.dispatcher()); status != ZX_OK) {
    LogError("Failed to being wait, crashsvc won't start", status);
    return status;
  }

  loop.Run();

  return 0;
}

}  // namespace

zx_status_t start_crashsvc(zx::job root_job, zx_handle_t exception_handler_svc, thrd_t* thread) {
  zx::channel exception_channel;
  if (const zx_status_t status = root_job.create_exception_channel(0, &exception_channel);
      status != ZX_OK) {
    LogError("failed to create exception channel", status);
    return status;
  }

  auto ctx = new crash_ctx{
      std::move(exception_channel),
      exception_handler_svc,
  };

  if (const zx_status_t status =
          thrd_status_to_zx_status(thrd_create_with_name(thread, crash_svc, ctx, "crash-svc"));
      status != ZX_OK) {
    delete ctx;
    return status;
  }
  return ZX_OK;
}
