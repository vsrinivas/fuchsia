// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/forensics/exceptions/pending_exception.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>

namespace forensics {
namespace exceptions {

PendingException::PendingException(async_dispatcher_t* dispatcher, zx::duration ttl,
                                   zx::exception exception)
    : exception_(std::move(exception)), process_(ZX_HANDLE_INVALID), thread_(ZX_HANDLE_INVALID) {
  if (!exception_.is_valid()) {
    return;
  }

  if (const zx_status_t status = exception_.get_process(&process_); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to get process; releasing the exception";
    exception_.reset();
  }

  if (const zx_status_t status = exception_.get_thread(&thread_); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to get thread; releasing the exception";
    exception_.reset();
  }

  if (const zx_status_t status = reset_.PostDelayed(dispatcher, ttl); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to post reset task for exception; releasing the exception";
    exception_.reset();
  }
}

zx::exception&& PendingException::TakeException() { return std::move(exception_); }
zx::process&& PendingException::TakeProcess() { return std::move(process_); }
zx::thread&& PendingException::TakeThread() { return std::move(thread_); }

void PendingException::Reset() { exception_.reset(); }

}  // namespace exceptions
}  // namespace forensics
