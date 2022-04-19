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
  //
  // Messages are assumed to be received mostly in order.
  bool Add(LogSink::MessageOr message) override;

  std::string ToString();

 private:
  struct Message {
    Message(const LogSink::MessageOr& message, int64_t default_timestamp);

    int64_t timestamp;
    std::string msg;
  };

  void Sort();
  void EnforceCapacity();

  RedactorBase* redactor_;
  std::deque<Message> messages_;

  std::string last_msg_;
  size_t last_msg_repeated_;

  bool is_sorted_{true};

  size_t size_{0u};
  const size_t capacity_;
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_SYSTEM_LOG_H_
