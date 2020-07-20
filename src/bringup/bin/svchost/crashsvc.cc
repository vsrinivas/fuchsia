// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/exception/llcpp/fidl.h>
#include <inttypes.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/backtrace-request/backtrace-request-utils.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/llcpp/client.h>
#include <lib/zx/channel.h>
#include <lib/zx/exception.h>
#include <lib/zx/handle.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <stdint.h>
#include <stdio.h>
#include <threads.h>
#include <zircon/status.h>
#include <zircon/syscalls/exception.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include <memory>

#include <crashsvc/crashsvc.h>
#include <inspector/inspector.h>

namespace {

struct crash_ctx {
  zx::channel exception_channel;
  zx::channel handler_client;
};

// Logs a general error unrelated to a particular exception.
void LogError(const char* message, zx_status_t status) {
  fprintf(stderr, "crashsvc: %s: %s (%d)\n", message, zx_status_get_string(status), status);
}

// Logs an error when handling the exception described by |info|.
void LogError(const char* message, const zx_exception_info& info, zx_status_t status) {
  fprintf(stderr, "crashsvc: %s [thread %" PRIu64 ".%" PRIu64 "]: %s (%d)\n", message, info.pid,
          info.tid, zx_status_get_string(status), status);
}

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

zx_status_t ConnectToExceptionHandler(zx_handle_t exception_handler_svc,
                                      zx::channel* handler_client) {
  // We are in a build without a server for fuchsia.exception.Handler, e.g., bringup.
  if (exception_handler_svc == ZX_HANDLE_INVALID) {
    return ZX_OK;
  }

  zx::channel handler_server;
  zx::channel::create(0u, &handler_server, handler_client);
  const zx_status_t status = fdio_service_connect_at(
      exception_handler_svc, llcpp::fuchsia::exception::Handler::Name, handler_server.release());

  if (status != ZX_OK) {
    LogError("unable to connect to exception handler service", status);
  }

  return status;
}

void HandOffException(
    zx::exception exception, const zx_exception_info_t& info,
    const std::unique_ptr<fidl::Client<llcpp::fuchsia::exception::Handler>>& exception_handler) {
  zx::process process;
  if (const zx_status_t status = exception.get_process(&process); status != ZX_OK) {
    LogError("failed to get exception process", info, status);
    return;
  }

  zx::thread thread;
  if (const zx_status_t status = exception.get_thread(&thread); status != ZX_OK) {
    LogError("failed to get exception thread", info, status);
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
  inspector_print_debug_info(stdout, process.get(), thread.get());

  // Send over the exception to the handler.
  // From this point on, crashsvc has no ownership over the exception and it's up to the handler to
  // decide when and how to resume it.
  if (exception_handler) {
    llcpp::fuchsia::exception::ExceptionInfo exception_info;
    exception_info.process_koid = info.pid;
    exception_info.thread_koid = info.tid;
    exception_info.type = static_cast<llcpp::fuchsia::exception::ExceptionType>(info.type);

    if (const auto result =
            (*exception_handler)->OnException(std::move(exception), exception_info, [] {});
        result.status() != ZX_OK) {
      LogError("failed to pass exception to handler", info, result.status());
    }
  }
}

int crash_svc(void* arg) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto ctx = std::unique_ptr<crash_ctx>(reinterpret_cast<crash_ctx*>(arg));

  auto exception_handler = (ctx->handler_client.is_valid())
                               ? std::make_unique<fidl::Client<llcpp::fuchsia::exception::Handler>>(
                                     std::move(ctx->handler_client), loop.dispatcher())
                               : nullptr;

  for (;;) {
    zx_signals_t signals = 0;
    if (const zx_status_t status = ctx->exception_channel.wait_one(
            ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &signals);
        status != ZX_OK) {
      LogError("failed to wait on the exception channel", status);
      continue;
    }

    fprintf(stdout, "crashsvc: exception received, processing\n");

    if (signals & ZX_CHANNEL_PEER_CLOSED) {
      // We should only get here in crashsvc's unit tests. In production, our job is actually the
      // root job so the system will halt before closing its exception channel.
      break;
    }

    zx_exception_info_t info;
    zx::exception exception;
    if (const zx_status_t status = ctx->exception_channel.read(
            0, &info, exception.reset_and_get_address(), sizeof(info), 1, nullptr, nullptr);
        status != ZX_OK) {
      LogError("failed to read from the exception channel", status);
      continue;
    }

    // Run the loop before handing off the exception to give queued tasks a chance to execute.
    if (const zx_status_t status = loop.RunUntilIdle(); status != ZX_OK) {
      LogError("failed to run async loop", status);
    }
    HandOffException(std::move(exception), info, exception_handler);
  }

  loop.Shutdown();
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

  zx::channel handler_client;
  if (const zx_status_t status = ConnectToExceptionHandler(exception_handler_svc, &handler_client);
      status != ZX_OK) {
    return status;
  }

  auto ctx = new crash_ctx{
      std::move(exception_channel),
      std::move(handler_client),
  };

  if (const zx_status_t status =
          thrd_status_to_zx_status(thrd_create_with_name(thread, crash_svc, ctx, "crash-svc"));
      status != ZX_OK) {
    delete ctx;
    return status;
  }
  return ZX_OK;
}
