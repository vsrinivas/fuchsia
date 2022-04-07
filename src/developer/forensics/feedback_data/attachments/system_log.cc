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

constexpr char kFormatFailedFormatPrefix[] = "!!! Failed to format chunk: ";
constexpr char kFormatFailedFormatSuffix[] = " !!!\n";

}  // namespace

LogBuffer::LogBuffer(const StorageSize capacity, RedactorBase* redactor)
    : redactor_(redactor), capacity_(capacity.ToBytes()) {}

bool LogBuffer::Add(LogSink::MessageOr message) {
  auto enforce_capacity = ::fit::defer([this] { EnforceCapacity(); });

  if (message.is_ok()) {
    message.value().msg = redactor_->Redact(message.value().msg);
  } else {
    message.error() = redactor_->Redact(message.error());
  }

  // Create the first message sequence. Errors are assumed to start at 0 if no valid message exists
  // before them.
  if (messages_at_time_.empty()) {
    auto& seq = message.is_ok() ? messages_at_time_[message.value().time] : messages_at_time_[0];
    size_ += seq.Add(std::move(message));
    return true;
  }

  // Check if |message| is a duplicate of the most recent message in the sequence with a timestamp
  // less than the timestamp of |message|. If so, add it to record the fact it's a duplicate
  // otherwise creata a new sequence.
  auto nearest_seq = (message.is_ok()) ? messages_at_time_.lower_bound(message.value().time)
                                       : messages_at_time_.begin();

  // Create a new sequence if there isn't a sequence before |message|.
  if (nearest_seq == messages_at_time_.end()) {
    auto& seq = message.is_ok() ? messages_at_time_[message.value().time]
                                : std::prev(messages_at_time_.end())->second;
    size_ += seq.Add(std::move(message));
    return true;
  }

  auto& seq = nearest_seq->second;
  if (message.is_error() || seq.MatchesLast(message)) {
    size_ += seq.Add(std::move(message));
  } else {
    size_ += messages_at_time_[message.value().time].Add(std::move(message));
  }

  return true;
}

std::string LogBuffer::ToString() const {
  std::string out;

  for (auto it = messages_at_time_.crbegin(); it != messages_at_time_.crend(); ++it) {
    it->second.Append(out);
  }

  return out;
}

void LogBuffer::EnforceCapacity() {
  if (size_ <= capacity_) {
    return;
  }

  auto it = std::prev(messages_at_time_.end());
  while (size_ > capacity_ && !messages_at_time_.empty()) {
    auto& seq = it->second;
    if (seq.IsEmpty()) {
      messages_at_time_.erase(it--);
      continue;
    }

    size_ -= seq.PopBytes(size_ - capacity_);
  }
}

bool LogBuffer::MessageSequence::MatchesLast(const LogSink::MessageOr& message) const {
  if (messages_.empty()) {
    return false;
  }

  if (message.is_error()) {
    return last_msg_ == message.error();
  }

  return last_msg_ == message.value().msg;
}

size_t LogBuffer::MessageSequence::Add(LogSink::MessageOr message) {
  if (MatchesLast(message)) {
    ++(messages_.back().second);
    return 0;
  }

  last_msg_ = (message.is_ok()) ? message.value().msg : message.error();

  std::string text = (message.is_ok())
                         ? Format(message.value())
                         : fxl::StringPrintf("%s%s%s", kFormatFailedFormatPrefix,
                                             message.error().c_str(), kFormatFailedFormatSuffix);
  const size_t size = text.size();

  messages_.emplace_back(std::move(text), 0);
  return size;
}

void LogBuffer::MessageSequence::Append(std::string& out) const {
  for (const auto& [message, repeated] : messages_) {
    out.append(message);
    if (repeated == 1) {
      out.append(kRepeatedOnceFormatStr);
    } else if (repeated > 1) {
      out.append(fxl::StringPrintf(kRepeatedFormatStr, repeated));
    }
  }
}

size_t LogBuffer::MessageSequence::PopBytes(const size_t bytes) {
  size_t removed{0u};
  while (removed < bytes && !IsEmpty()) {
    auto& [message, repeated] = messages_.front();

    removed += message.size();

    messages_.pop_front();
  }

  return removed;
}

bool LogBuffer::MessageSequence::IsEmpty() const { return messages_.empty(); }

}  // namespace feedback_data
}  // namespace forensics
