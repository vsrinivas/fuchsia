// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/attachments/kernel_log_ptr.h"

#include <zircon/syscalls/log.h>

#include "src/developer/feedback/utils/cobalt_metrics.h"
#include "src/developer/feedback/utils/promise.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

fit::promise<AttachmentValue> CollectKernelLog(async_dispatcher_t* dispatcher,
                                               std::shared_ptr<sys::ServiceDirectory> services,
                                               zx::duration timeout, Cobalt* cobalt) {
  std::unique_ptr<BootLog> boot_log = std::make_unique<BootLog>(dispatcher, services, cobalt);

  // We must store the promise in a variable due to the fact that the order of evaluation of
  // function parameters is undefined.
  auto logs = boot_log->GetLog(timeout);
  return ExtendArgsLifetimeBeyondPromise(/*promise=*/std::move(logs), /*args=*/std::move(boot_log));
}

BootLog::BootLog(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
                 Cobalt* cobalt)
    : services_(services), cobalt_(cobalt), bridge_(dispatcher, "Kernel log collection") {}

fit::promise<AttachmentValue> BootLog::GetLog(const zx::duration timeout) {
  FXL_CHECK(!has_called_get_log_) << "GetLog() is not intended to be called twice";
  has_called_get_log_ = true;

  log_ptr_ = services_->Connect<fuchsia::boot::ReadOnlyLog>();

  log_ptr_.set_error_handler([this](zx_status_t status) {
    if (bridge_.IsAlreadyDone()) {
      return;
    }

    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.boot.ReadOnlyLog";
    bridge_.CompleteError();
  });

  log_ptr_->Get([this](zx::debuglog log) {
    if (bridge_.IsAlreadyDone()) {
      return;
    }

    // zx_log_record_t has a flexible array member, so we need to allocate the buffer explicitly.
    char buf[ZX_LOG_RECORD_MAX + 1];
    zx_log_record_t* record = reinterpret_cast<zx_log_record_t*>(buf);
    std::string kernel_log;
    while (log.read(/*options=*/0, /*buffer=*/record,
                    /*buffer_size=*/ZX_LOG_RECORD_MAX) > 0) {
      if (record->datalen && (record->data[record->datalen - 1] == '\n')) {
        record->datalen--;
      }
      record->data[record->datalen] = 0;

      kernel_log += fxl::StringPrintf("[%05d.%03d] %05" PRIu64 ".%05" PRIu64 "> %s\n",
                                      static_cast<int>(record->timestamp / 1000000000ULL),
                                      static_cast<int>((record->timestamp / 1000000ULL) % 1000ULL),
                                      record->pid, record->tid, record->data);
    }

    if (kernel_log.empty()) {
      FX_LOGS(ERROR) << "Empty kernel log";
      bridge_.CompleteError();
      return;
    }

    bridge_.CompleteOk(kernel_log);
  });

  return bridge_.WaitForDone(
      timeout, /*if_timeout=*/[this] { cobalt_->LogOccurrence(TimedOutData::kKernelLog); });
}

}  // namespace feedback
