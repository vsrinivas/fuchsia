// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/attachments/kernel_log_ptr.h"

#include <inttypes.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/syscalls/log.h>

#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/fit/promise.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace feedback_data {

::fpromise::promise<AttachmentValue> CollectKernelLog(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    fit::Timeout timeout) {
  std::unique_ptr<BootLog> boot_log = std::make_unique<BootLog>(dispatcher, services);

  // We must store the promise in a variable due to the fact that the order of evaluation of
  // function parameters is undefined.
  auto logs = boot_log->GetLog(std::move(timeout));
  return fit::ExtendArgsLifetimeBeyondPromise(/*promise=*/std::move(logs),
                                              /*args=*/std::move(boot_log));
}

BootLog::BootLog(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services)
    : log_ptr_(dispatcher, services) {}

::fpromise::promise<AttachmentValue> BootLog::GetLog(fit::Timeout timeout) {
  log_ptr_->Get([this](zx::debuglog log) {
    if (log_ptr_.IsAlreadyDone()) {
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
      log_ptr_.CompleteError(Error::kMissingValue);
      return;
    }

    log_ptr_.CompleteOk(kernel_log);
  });

  return log_ptr_.WaitForDone(std::move(timeout))
      .then([](const ::fpromise::result<std::string, Error>& result) {
        AttachmentValue value =
            (result.is_ok()) ? AttachmentValue(result.value()) : AttachmentValue(result.error());
        return ::fpromise::ok(std::move(value));
      });
}

}  // namespace feedback_data
}  // namespace forensics
