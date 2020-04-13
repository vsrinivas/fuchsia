// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_SYSTEM_LOG_RECORDER_LOG_MESSAGE_STORE_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_SYSTEM_LOG_RECORDER_LOG_MESSAGE_STORE_H_

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

  // Adds the log message to the store. The message will be dropped if the store does not have
  // enough capacity remaining for it and return false.
  bool Add(std::string message);

  // Consumes the contents of the store as a string. This will empty the store.
  std::string Consume();

 private:
  std::mutex mtx_;
  std::deque<std::string> queue_;

  const size_t max_capacity_bytes_;

  size_t bytes_remaining_;
  size_t num_messages_dropped = 0;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_SYSTEM_LOG_RECORDER_LOG_MESSAGE_STORE_H_
