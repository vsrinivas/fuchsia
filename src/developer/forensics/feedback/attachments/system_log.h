// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ATTACHMENTS_SYSTEM_LOG_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ATTACHMENTS_SYSTEM_LOG_H_

#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/fpromise/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <deque>
#include <memory>
#include <optional>
#include <string>

#include "src/developer/forensics/feedback/attachments/provider.h"
#include "src/developer/forensics/feedback/attachments/types.h"
#include "src/developer/forensics/feedback_data/log_source.h"
#include "src/developer/forensics/utils/redact/redactor.h"
#include "src/developer/forensics/utils/storage_size.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/timekeeper/clock.h"

namespace forensics::feedback {

// Stores up to |capacity| bytes of system log messages, dropping the earliest messages when the
// stored messages occupy too much space.
class LogBuffer : public feedback_data::LogSink {
 public:
  LogBuffer(StorageSize capacity, RedactorBase* redactor);

  virtual ~LogBuffer() = default;

  // Adds |message| to the buffer and drops messages as required to keep the total size under
  // |capacity|. Always returns true.
  //
  // Messages are assumed to be received mostly in order.
  bool Add(LogSink::MessageOr message) override;

  // Records the log stream was interrupted and clears the contents.
  void NotifyInterruption() override;

  // It's safe continue to writing to a LogBuffer if the log source has been interrupted.
  bool SafeAfterInterruption() const override { return true; }

  std::string ToString();

  // Executes |action| after a message with a time greater than or equal to |timestamp| is received
  // or NotifyInterruption is called.
  void ExecuteAfter(zx::duration timestamp, ::fit::closure action);

 private:
  struct Message {
    Message(const LogSink::MessageOr& message, int64_t default_timestamp);

    int64_t timestamp;
    std::string msg;
  };

  void Sort();
  void RunActions(int64_t timestamp);
  void EnforceCapacity();

  // Resets variables keeping track of the last message
  void ResetLastMessage();

  RedactorBase* redactor_;
  std::deque<Message> messages_;

  std::string last_msg_{};
  int32_t last_severity_{};
  std::vector<std::string> last_tags{};
  size_t last_msg_repeated_{0u};

  bool is_sorted_{true};

  std::multimap<int64_t, ::fit::closure, std::greater<>> actions_at_time_;

  size_t size_{0u};
  const size_t capacity_;
};

// Collects the system log.
//
// The system log is streamed and buffered on the first call to Get and continues streaming until
// |active_period_| past the end of the call elapses.
//
// fuchsia.diagnostics.FeedbackArchiveAccessor is expected to be in |services|.
class SystemLog : public AttachmentProvider {
 public:
  SystemLog(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
            timekeeper::Clock* clock, RedactorBase* redactor, zx::duration active_period);

  ::fpromise::promise<AttachmentValue> Get(zx::duration timeout) override;

  // Returns a promise to the system log and allows collection to be terminated early with |ticket|.
  ::fpromise::promise<AttachmentValue> Get(uint64_t ticket, zx::duration timeout);

  // Completes the system log collection promise associated with |ticket| early, if it hasn't
  // already completed.
  void ForceCompletion(uint64_t ticket, Error error);

 private:
  // Terminates the stream and flushes the in-memory buffer.
  void MakeInactive();

  async_dispatcher_t* dispatcher_;

  LogBuffer buffer_;
  feedback_data::LogSource source_;

  timekeeper::Clock* clock_;

  zx::duration active_period_;
  bool is_active_{false};

  std::map<uint64_t, std::function<void(Error)>> completers_;
  uint64_t internal_ticket_{std::numeric_limits<uint64_t>::max()};

  async::TaskClosureMethod<SystemLog, &SystemLog::MakeInactive> make_inactive_{this};
  fxl::WeakPtrFactory<SystemLog> ptr_factory_{this};
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ATTACHMENTS_SYSTEM_LOG_H_
