// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/attachments/system_log_ptr.h"

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/result.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>
#include <string>
#include <vector>

#include "src/developer/forensics/feedback_data/archive_accessor_ptr.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/fit/promise.h"
#include "src/developer/forensics/utils/fit/timeout.h"
#include "src/developer/forensics/utils/log_format.h"
#include "src/lib/diagnostics/stream/cpp/log_message.h"
#include "src/lib/fxl/strings/join_strings.h"

namespace forensics {
namespace feedback_data {
namespace {

// A structured log message and the text errors that will appear after it.
struct LogMessage {
  std::optional<fuchsia::logger::LogMessage> message;
  std::vector<std::string> errors;
};

}  // namespace

::fit::promise<AttachmentValue> CollectSystemLog(async_dispatcher_t* dispatcher,
                                                 std::shared_ptr<sys::ServiceDirectory> services,
                                                 fit::Timeout timeout) {
  auto log_service =
      std::make_unique<ArchiveAccessor>(dispatcher, services, fuchsia::diagnostics::DataType::LOGS,
                                        fuchsia::diagnostics::StreamMode::SNAPSHOT);

  auto log_messages = std::make_shared<std::vector<LogMessage>>();

  // Add a text error to the last LogMessage in |log_messages|.
  auto AddError = [log_messages](const std::string& error) {
    if (log_messages->empty()) {
      log_messages->push_back(LogMessage{.message = std::nullopt, .errors = {}});
    }
    log_messages->back().errors.push_back(error);
  };

  // System log collection task.
  log_service->Collect([log_messages, AddError](fuchsia::diagnostics::FormattedContent chunk) {
    auto chunk_result = diagnostics::stream::ConvertFormattedContentToLogMessages(std::move(chunk));
    if (chunk_result.is_error()) {
      AddError(
          fxl::StringPrintf("!!! Failed to format chunk: %s !!!", chunk_result.error().c_str()));
      return;
    }

    for (auto& log_result : chunk_result.value()) {
      if (log_result.is_error()) {
        AddError(
            fxl::StringPrintf("!!! Failed to format chunk: %s !!!", log_result.error().c_str()));
        continue;
      }

      // Stable-sort |log_messages| by timestamp when adding a new message. If a message in
      // |log_messages| only has errors, assume the new message comes after it. Sorting is stable
      // and done on insertion because log messages are received mostly in order and messages with
      // the same timestamp should not be reordered.
      auto it = log_messages->crbegin();
      while (it != log_messages->crend() && it->message.has_value() &&
             log_result.value().time < it->message.value().time) {
        ++it;
      }
      log_messages->insert(it.base(), LogMessage{.message = log_result.take_value(), .errors = {}});
    }
  });

  // Post-collection task.
  ::fit::promise<AttachmentValue> log_promise =
      log_service->WaitForDone(std::move(timeout))
          .then(
              [log_messages](::fit::result<void, Error>& result) -> ::fit::result<AttachmentValue> {
                if (log_messages->empty()) {
                  FX_LOGS(WARNING) << "Empty system log";
                  AttachmentValue value = (result.is_ok()) ? AttachmentValue(Error::kMissingValue)
                                                           : AttachmentValue(result.error());
                  return ::fit::ok(std::move(value));
                }

                // Format the log messages by converting them to text and appending errors on their
                // own lines.
                std::vector<std::string> formatted_logs;
                for (const auto& log_message : *log_messages) {
                  std::string formatted =
                      (log_message.message.has_value()) ? Format(log_message.message.value()) : "";

                  if (!log_message.errors.empty()) {
                    formatted += fxl::JoinStrings(log_message.errors, "\n") + "\n";
                  }
                  formatted_logs.push_back(std::move(formatted));
                }

                std::string joined_data = fxl::JoinStrings(formatted_logs, "");

                AttachmentValue value =
                    (result.is_ok()) ? AttachmentValue(std::move(joined_data))
                                     : AttachmentValue(std::move(joined_data), result.error());

                return ::fit::ok(std::move(value));
              });

  return fit::ExtendArgsLifetimeBeyondPromise(/*promise=*/std::move(log_promise),
                                              /*args=*/std::move(log_service));
}

}  // namespace feedback_data
}  // namespace forensics
