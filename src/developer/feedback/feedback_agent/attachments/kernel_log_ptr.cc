// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/attachments/kernel_log_ptr.h"

#include <zircon/syscalls/log.h>

#include "src/developer/feedback/utils/promise.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

fit::promise<fuchsia::mem::Buffer> CollectKernelLog(async_dispatcher_t* dispatcher,
                                                    std::shared_ptr<sys::ServiceDirectory> services,
                                                    zx::duration timeout) {
  std::unique_ptr<BootLog> boot_log = std::make_unique<BootLog>(dispatcher, services);

  // We must store the promise in a variable due to the fact that the order of evaluation of
  // function parameters is undefined.
  auto logs = boot_log->GetLog(timeout);
  return ExtendArgsLifetimeBeyondPromise(/*promise=*/std::move(logs), /*args=*/std::move(boot_log));
}

BootLog::BootLog(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services)
    : dispatcher_(dispatcher), services_(services) {}

fit::promise<fuchsia::mem::Buffer> BootLog::GetLog(const zx::duration timeout) {
  FXL_CHECK(!has_called_get_log_) << "GetLog() is not intended to be called twice";
  has_called_get_log_ = true;

  log_ptr_ = services_->Connect<fuchsia::boot::ReadOnlyLog>();

  // fit::promise does not have the notion of a timeout. So we post a delayed task that will call
  // the completer after the timeout and return an error.
  //
  // We wrap the delayed task in a CancelableClosure so we can cancel it when the fit::bridge is
  // completed another way.
  //
  // It is safe to pass "this" to the fit::function as the callback won't be callable when the
  // CancelableClosure goes out of scope, which is before "this".
  done_after_timeout_.Reset([this] {
    if (!done_.completer) {
      return;
    }

    FX_LOGS(ERROR) << "Kernel log get timed out";
    done_.completer.complete_error();
  });
  const zx_status_t post_status = async::PostDelayedTask(
      dispatcher_, [cb = done_after_timeout_.callback()] { cb(); }, timeout);
  if (post_status != ZX_OK) {
    FX_PLOGS(ERROR, post_status) << "Failed to post delayed cancellation task";
    return fit::make_result_promise<fuchsia::mem::Buffer>(fit::error());
  }

  log_ptr_.set_error_handler([this](zx_status_t status) {
    if (!done_.completer) {
      return;
    }

    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.boot.ReadOnlyLog";
    done_.completer.complete_error();
  });

  log_ptr_->Get([this](zx::debuglog log) {
    if (!done_.completer) {
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
      done_.completer.complete_error();
      return;
    }

    fsl::SizedVmo vmo;
    if (!fsl::VmoFromString(kernel_log, &vmo)) {
      FX_LOGS(ERROR) << "Failed to convert kernel log string to vmo";
      done_.completer.complete_error();
      return;
    }
    done_.completer.complete_ok(std::move(vmo).ToTransport());
  });

  return done_.consumer.promise_or(fit::error())
      .then([this](fit::result<fuchsia::mem::Buffer>& result) {
        done_after_timeout_.Cancel();
        return std::move(result);
      });
}

}  // namespace feedback
