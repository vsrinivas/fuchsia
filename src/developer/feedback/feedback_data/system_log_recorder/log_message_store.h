// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_LOG_MESSAGE_STORE_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_LOG_MESSAGE_STORE_H_

#include <fuchsia/logger/cpp/fidl.h>

#include <deque>
#include <mutex>

namespace feedback {

// Thread-safe store of log messages.
//
// The store has a maximum capacity in bytes. The current size is measured as the sum of the size
// of each accumulated message that has not been consumed yet.
//
// Clients can add messages to the store or consume all of the added messages as a single string
// with an extra line at the end containing the number of messages that were failed to be added.
class LogMessageStore {
 public:
  LogMessageStore(size_t max_capacity_bytes);

  // May add the log message to the store:
  // * The message is dropped if the store has reached its maximum capacity, returning false.
  // * The message is omitted if it is the same one as the previous one in the store.
  bool Add(fuchsia::logger::LogMessage log);

  // Consumes the contents of the store as a string. This will empty the store.
  std::string Consume();

 private:
  std::mutex mtx_;
  std::deque<std::string> queue_;

  const size_t max_capacity_bytes_;

  size_t bytes_remaining_;
  size_t num_messages_dropped_ = 0;
  size_t last_pushed_message_count_ = 0;
  std::string last_pushed_message_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_LOG_MESSAGE_STORE_H_
