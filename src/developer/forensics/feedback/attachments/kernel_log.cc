// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/attachments/kernel_log.h"

#include <inttypes.h>
#include <lib/async/cpp/task.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/promise.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/syscalls/log.h>

#include <variant>

#include "src/developer/forensics/utils/errors.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics::feedback {

KernelLog::KernelLog(async_dispatcher_t* dispatcher,
                     std::shared_ptr<sys::ServiceDirectory> services,
                     std::unique_ptr<backoff::Backoff> backoff, RedactorBase* redactor)
    : dispatcher_(dispatcher),
      services_(std::move(services)),
      backoff_(std::move(backoff)),
      redactor_(redactor) {
  read_only_log_.set_error_handler([this](const zx_status_t status) {
    FX_PLOGS(WARNING, status) << "Lost connection to fuchsia.boot.ReadOnlyLog";

    for (auto& waiting : waiting_) {
      if (waiting != nullptr) {
        waiting(Error::kConnectionError);
      }
    }

    auto self = ptr_factory_.GetWeakPtr();
    async::PostDelayedTask(
        dispatcher_,
        [self] {
          if (self) {
            self->services_->Connect(self->read_only_log_.NewRequest(self->dispatcher_));
          }
        },
        backoff_->GetNext());
  });

  services_->Connect(read_only_log_.NewRequest(dispatcher_));
}

::fpromise::promise<AttachmentValue> KernelLog::Get(const zx::duration timeout) {
  return Get(internal_ticket_--, timeout);
}

::fpromise::promise<AttachmentValue> KernelLog::Get(const uint64_t ticket,
                                                    const zx::duration timeout) {
  FX_CHECK(completers_.count(ticket) == 0) << "Ticket used twice: " << ticket;

  if (!read_only_log_.is_bound()) {
    return ::fpromise::make_ok_promise(AttachmentValue(Error::kConnectionError));
  }

  ::fpromise::bridge<zx::debuglog, Error> bridge;

  // Construct a promise and an object that can be used to complete the promise with a value at a
  // later point in time.
  auto consume = bridge.consumer.promise_or(::fpromise::error(Error::kLogicError));
  ::fit::callback<void(std::variant<zx::debuglog, Error>)> complete =
      [completer = std::move(bridge.completer)](std::variant<zx::debuglog, Error> result) mutable {
        if (std::holds_alternative<zx::debuglog>(result)) {
          completer.complete_ok(std::move(std::get<zx::debuglog>(result)));
        } else {
          completer.complete_error(std::get<Error>(result));
        }
      };

  completers_[ticket] = complete.share();

  auto self = ptr_factory_.GetWeakPtr();
  async::PostDelayedTask(
      dispatcher_,
      [self, ticket]() mutable {
        if (self) {
          self->ForceCompletion(ticket, Error::kTimeout);
        }
      },
      timeout);

  read_only_log_->Get([complete = complete.share()](zx::debuglog debuglog) mutable {
    if (complete != nullptr) {
      complete(std::move(debuglog));
    }
  });

  return consume
      .and_then([self, ticket, this](zx::debuglog& debuglog) {
        waiting_.erase(std::remove(waiting_.begin(), waiting_.end(), nullptr), waiting_.end());

        std::vector<std::string> messages;

        std::array<char, ZX_LOG_RECORD_MAX + 1> buf;
        auto* record = reinterpret_cast<zx_log_record_t*>(buf.data());

        while (debuglog.read(/*options=*/0, /*buffer=*/record,
                             /*buffer_size=*/ZX_LOG_RECORD_MAX) > 0) {
          if (auto& len = record->datalen; len != 0 && record->data[len - 1] == '\n') {
            --len;
          }
          record->data[record->datalen] = 0;

          std::string data(record->data);
          data = redactor_->Redact(data);
          messages.push_back(
              fxl::StringPrintf("[%05d.%03d] %05" PRIu64 ".%05" PRIu64 "> %s\n",
                                static_cast<int>(record->timestamp / 1000000000ULL),
                                static_cast<int>((record->timestamp / 1000000ULL) % 1000ULL),
                                record->pid, record->tid, data.c_str()));
        }

        if (self) {
          self->completers_.erase(ticket);
        }

        if (messages.empty()) {
          FX_LOGS(ERROR) << "Empty kernel log";
          return ::fpromise::ok(AttachmentValue(Error::kMissingValue));
        }

        return ::fpromise::ok(AttachmentValue(fxl::JoinStrings(messages)));
      })
      .or_else([](const Error& error) { return ::fpromise::ok(AttachmentValue(error)); });
}

void KernelLog::ForceCompletion(const uint64_t ticket, const Error error) {
  if (completers_.count(ticket) != 0 && completers_[ticket] != nullptr) {
    completers_[ticket](error);
  }
}

}  // namespace forensics::feedback
