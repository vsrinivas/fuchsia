// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <crashsvc/crashsvc.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/crash/c/fidl.h>
#include <inspector/inspector.h>
#include <inttypes.h>
#include <lib/backtrace-request/backtrace-request-utils.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
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

namespace {

struct crash_ctx {
  zx::channel exception_channel;
  zx::channel svc_request;
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
    zx_status_t status = cleanup_backtrace_request(thread.get(), regs);
    if (status != ZX_OK) {
      LogError("failed to cleanup backtrace", info, status);
      return false;
    }

    // Mark the exception as handled so the thread resumes execution.
    uint32_t state = ZX_EXCEPTION_STATE_HANDLED;
    status = exception.set_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state));
    if (status != ZX_OK) {
      LogError("failed to resume from backtrace", info, status);
      return false;
    }

    return true;
  }

  return false;
}

void HandOffException(zx::exception exception, const zx_exception_info_t& info,
                      const zx::channel& svc_request) {
  zx::process process;
  zx_status_t status = exception.get_process(&process);
  if (status != ZX_OK) {
    LogError("failed to get exception process", info, status);
    return;
  }

  zx::thread thread;
  status = exception.get_thread(&thread);
  if (status != ZX_OK) {
    LogError("failed to get exception thread", info, status);
    return;
  }

  // Dump the crash info to the logs whether we have a FIDL analyzer or not.
  zx_thread_state_general_regs_t regs = {};
  inspector_print_debug_info(process.get(), thread.get(), &regs);

  // A backtrace request should just dump and continue.
  if (ResumeIfBacktraceRequest(thread, exception, info, &regs)) {
    return;
  }

  if (svc_request.is_valid()) {
    // Use the full system analyzer FIDL service, presumably crashpad_analyzer.
    fuchsia_crash_Analyzer_OnNativeException_Result analyzer_result;
    status = fuchsia_crash_AnalyzerOnNativeException(svc_request.get(), process.release(),
                                                     thread.release(), &analyzer_result);
    if (status != ZX_OK) {
      LogError("failed to pass exception to analyzer", info, status);
    }
  }

  // When |exception| goes out of scope it will close and cause the exception
  // to be sent to the next handler. If we're the root job handler this will
  // kill the process.
}

int crash_svc(void* arg) {
  auto ctx = fbl::unique_ptr<crash_ctx>(reinterpret_cast<crash_ctx*>(arg));

  for (;;) {
    zx_signals_t signals = 0;
    zx_status_t status = ctx->exception_channel.wait_one(
        ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &signals);
    if (status != ZX_OK) {
      LogError("failed to wait on the exception channel", status);
      continue;
    }

    if (signals & ZX_CHANNEL_PEER_CLOSED) {
      // We'll only get here in tests, if our job is actually the root job
      // the system will halt before closing the channel.
      return 0;
    }

    zx_exception_info_t info;
    zx::exception exception;
    status = ctx->exception_channel.read(0, &info, exception.reset_and_get_address(), sizeof(info),
                                         1, nullptr, nullptr);
    if (status != ZX_OK) {
      LogError("failed to read from the exception channel", status);
      continue;
    }

    HandOffException(std::move(exception), info, ctx->svc_request);
  }
}

}  // namespace

zx_status_t start_crashsvc(zx::job root_job, zx_handle_t analyzer_svc, thrd_t* thread) {
  zx::channel exception_channel;
  zx_status_t status = root_job.create_exception_channel(0, &exception_channel);
  if (status != ZX_OK) {
    LogError("failed to create exception channel", status);
    return status;
  }

  zx::channel ch0, ch1;
  if (analyzer_svc != ZX_HANDLE_INVALID) {
    zx::channel::create(0u, &ch0, &ch1);
    status = fdio_service_connect_at(analyzer_svc, fuchsia_crash_Analyzer_Name, ch0.release());
    if (status != ZX_OK) {
      LogError("unable to connect to analyzer service", status);
      return status;
    }
  }

  auto ctx = new crash_ctx{
      std::move(exception_channel),
      std::move(ch1),
  };

  status = thrd_status_to_zx_status(thrd_create_with_name(thread, crash_svc, ctx, "crash-svc"));
  if (status != ZX_OK) {
    delete ctx;
  }
  return status;
}
