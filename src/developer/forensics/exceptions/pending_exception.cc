// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/forensics/exceptions/pending_exception.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/process.h>

#include "src/lib/fsl/handles/object_info.h"

namespace forensics {
namespace exceptions {

PendingException::PendingException(async_dispatcher_t* dispatcher, zx::duration ttl,
                                   zx::exception exception)
    : exception_(std::move(exception)) {
  zx::process process;
  if (const zx_status_t status = exception_.get_process(&process); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to get process; releasing the exception";
    exception_.reset();
  }

  crashed_process_name_ =
      (process.is_valid()) ? fsl::GetObjectName(process.get()) : "unknown_process";
  crashed_process_koid_ = std::to_string(fsl::GetKoid(process.get()));

  if (const zx_status_t status = reset_.PostDelayed(dispatcher, ttl); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to post reset task for exception; releasing the exception";
    exception_.reset();
  }
}

zx::exception&& PendingException::TakeException() { return std::move(exception_); }

std::string PendingException::CrashedProcessName() const { return crashed_process_name_; }

std::string PendingException::CrashedProcessKoid() const { return crashed_process_koid_; }

void PendingException::Reset() { exception_.reset(); }

}  // namespace exceptions
}  // namespace forensics
