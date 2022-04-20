// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/developer/forensics/feedback_data/attachments/system_log.h"

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/defer.h>
#include <lib/fpromise/result.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>
#include <string>
#include <vector>

#include "src/developer/forensics/feedback_data/archive_accessor_ptr.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/fit/promise.h"
#include "src/developer/forensics/utils/fit/timeout.h"
#include "src/developer/forensics/utils/log_format.h"
#include "src/lib/diagnostics/accessor2logger/log_message.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace feedback_data {

::fpromise::promise<AttachmentValue> CollectSystemLog(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    fit::Timeout timeout, RedactorBase* redactor) {
  auto log_service =
      std::make_unique<ArchiveAccessor>(dispatcher, services, fuchsia::diagnostics::DataType::LOGS,
                                        fuchsia::diagnostics::StreamMode::SNAPSHOT);

  // The system log shouldn't exceed 4 MiB, but use 10 MiB as a precaution.
  auto buffer = std::make_shared<LogBuffer>(StorageSize::Megabytes(10), redactor);

  // System log collection task.
  log_service->Collect([buffer](fuchsia::diagnostics::FormattedContent chunk) {
    auto chunk_result =
        diagnostics::accessor2logger::ConvertFormattedContentToLogMessages(std::move(chunk));
    if (chunk_result.is_error()) {
      buffer->Add(::fpromise::error(std::move(chunk_result.error())));
      return;
    }

    for (auto& log : chunk_result.value()) {
      buffer->Add(std::move(log));
    }
  });

  // Post-collection task.
  ::fpromise::promise<AttachmentValue> log_promise =
      log_service->WaitForDone(std::move(timeout))
          .then([buffer](::fpromise::result<void, Error>& result)
                    -> ::fpromise::result<AttachmentValue> {
            std::string log = buffer->ToString();
            if (log.empty()) {
              FX_LOGS(WARNING) << "Empty system log";
              AttachmentValue value = (result.is_ok()) ? AttachmentValue(Error::kMissingValue)
                                                       : AttachmentValue(result.error());
              return ::fpromise::ok(std::move(value));
            }

            AttachmentValue value = (result.is_ok())
                                        ? AttachmentValue(std::move(log))
                                        : AttachmentValue(std::move(log), result.error());

            return ::fpromise::ok(std::move(value));
          });

  return fit::ExtendArgsLifetimeBeyondPromise(/*promise=*/std::move(log_promise),
                                              /*args=*/std::move(log_service));
}

namespace {

size_t AppendRepeated(const size_t last_msg_repeated, std::string& append_to) {
  auto repeated_str = last_msg_repeated == 1
                          ? kRepeatedOnceFormatStr
                          : fxl::StringPrintf(kRepeatedFormatStr, last_msg_repeated);

  append_to.append(repeated_str);
  return repeated_str.size();
}

}  // namespace

LogBuffer::LogBuffer(const StorageSize capacity, RedactorBase* redactor)
    : redactor_(redactor), capacity_(capacity.ToBytes()) {}

bool LogBuffer::Add(LogSink::MessageOr message) {
  if (was_interrupted_) {
    messages_.clear();
    last_msg_ = "";
    last_msg_repeated_ = 0u;
    is_sorted_ = true;
    was_interrupted_ = false;
    size_ = 0u;
  }

  auto enforce_capacity = ::fit::defer([this] { EnforceCapacity(); });

  if (message.is_ok()) {
    redactor_->Redact(message.value().msg);
  } else {
    redactor_->Redact(message.error());
  }

  // Assume timestamp 0 if no messages have been added yet.
  const int64_t last_timestamp = (messages_.empty()) ? 0 : messages_.back().timestamp;
  const auto& msg = (message.is_ok()) ? message.value().msg : message.error();

  // Adds a new message to |messages_| and updates internal accounting.
  auto AddNew = [this, &message, &msg, last_timestamp] {
    messages_.emplace_back(message, last_timestamp);
    size_ += messages_.back().msg.size();

    last_msg_ = msg;
    last_msg_repeated_ = 0;
    is_sorted_ &= messages_.back().timestamp >= last_timestamp;

    return true;
  };

  if (messages_.empty()) {
    return AddNew();
  }

  // The most recent message is repeated, don't need to create new data.
  if (last_msg_ == msg) {
    ++last_msg_repeated_;
    return true;
  }

  // Inject a signal the most previously added message was repeated.
  if (last_msg_repeated_ > 0) {
    size_ += AppendRepeated(last_msg_repeated_, messages_.back().msg);
  }

  return AddNew();
}

void LogBuffer::NotifyInterruption() { was_interrupted_ = true; }

std::string LogBuffer::ToString() {
  // Ensure messages appear in time order.
  Sort();

  std::string out;
  out.reserve(size_);
  for (const auto& message : messages_) {
    out.append(message.msg);
  }

  // Inject a signal the last message was repeated because the signal doesn't exist in the
  // log yet.
  if (last_msg_repeated_ > 0) {
    AppendRepeated(last_msg_repeated_, out);
  }

  return out;
}

void LogBuffer::Sort() {
  // No sort is needed.
  if (is_sorted_) {
    return;
  }

  // Inject a signal the last message was repeated because the sort may change which message is
  // last.
  if (last_msg_repeated_ > 0) {
    size_ += AppendRepeated(last_msg_repeated_, messages_.back().msg);
  }

  std::stable_sort(messages_.begin(), messages_.end(),
                   [](const auto& lhs, const auto& rhs) { return lhs.timestamp < rhs.timestamp; });
  is_sorted_ = true;

  // Reset the message last added.
  //
  // Note: info used to deduplicate messages is lost; it has not yet been proven important enough
  // in the system log to justify the cost of identifying what the original msg was and
  // aggregating all adjacent messages that match it. For example, it may be possible to see the
  // sequence:
  //
  // LOG MESSAGE A
  // !!! MESSAGE REPEATED 3 MORE TIMES!!!
  // LOG MESSAGE A
  //
  // in a final system log.
  last_msg_ = "";
  last_msg_repeated_ = 0;
}

void LogBuffer::EnforceCapacity() {
  if (size_ <= capacity_) {
    return;
  }

  // Ensure messages are dropped in time order.
  Sort();
  while (size_ > capacity_ && !messages_.empty()) {
    size_ -= messages_.front().msg.size();
    messages_.pop_front();
  }
}

LogBuffer::Message::Message(const LogSink::MessageOr& message, int64_t default_timestamp)
    : timestamp(message.is_ok() ? message.value().time : default_timestamp),
      msg(message.is_ok() ? Format(message.value())
                          : fxl::StringPrintf("!!! Failed to format chunk: %s !!!\n",
                                              message.error().c_str())) {}

}  // namespace feedback_data
}  // namespace forensics
