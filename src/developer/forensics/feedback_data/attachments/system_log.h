// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_SYSTEM_LOG_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_SYSTEM_LOG_H_

#include <lib/async/dispatcher.h>
#include <lib/fpromise/promise.h>
#include <lib/sys/cpp/service_directory.h>

#include <deque>
#include <memory>
#include <optional>
#include <string>

#include "src/developer/forensics/feedback_data/attachments/types.h"
#include "src/developer/forensics/feedback_data/log_source.h"
#include "src/developer/forensics/utils/fit/timeout.h"
#include "src/developer/forensics/utils/redact/redactor.h"
#include "src/developer/forensics/utils/storage_size.h"

namespace forensics {
namespace feedback_data {

// Collects the system log.
//
// fuchsia.diagnostics.FeedbackArchiveAccessor is expected to be in |services|.
// |redactor| must not be deleted until after the returned promise completes.
::fpromise::promise<AttachmentValue> CollectSystemLog(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    fit::Timeout timeout, RedactorBase* redactor);

// Stores up to |capacity| bytes of system log messages, dropping the earliest messages when the
// stored messages occupy too much space.
class LogBuffer : public LogSink {
 public:
  LogBuffer(StorageSize capacity, RedactorBase* redactor);

  virtual ~LogBuffer() = default;

  // Adds |message| to the buffer and drops messages as required to keep the total size under
  // |capacity|. Always returns true.
  bool Add(LogSink::MessageOr message) override;

  std::string ToString() const;

 private:
  // A sequence of messages at a specific point in time.
  struct MessageSequence {
   public:
    // Add |message| and returns the number of bytes added.
    size_t Add(LogSink::MessageOr message);

    // Pop |bytes| bytes of messages from the sequence and returns the number of bytes popped.
    size_t PopBytes(size_t bytes);

    bool IsEmpty() const;
    void Append(std::string& out) const;

    // Returns true if |message| is duplicate of the last message in the sequence.
    bool MatchesLast(const LogSink::MessageOr& message) const;

   private:
    std::string last_msg_{};
    std::deque<std::pair<std::string, size_t>> messages_;
  };

  void EnforceCapacity();
  void AddMessage(LogSink::MessageOr message);

  RedactorBase* redactor_;
  std::map<int64_t, MessageSequence, std::greater<>> messages_at_time_;

  size_t size_{0u};
  const size_t capacity_;
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_SYSTEM_LOG_H_
