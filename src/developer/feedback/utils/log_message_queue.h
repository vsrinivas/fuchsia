// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_UTILS_LOG_MESSAGE_QUEUE_H_
#define SRC_DEVELOPER_FEEDBACK_UTILS_LOG_MESSAGE_QUEUE_H_

#include <fuchsia/logger/cpp/fidl.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <queue>

namespace feedback {

class LogMessageQueue {
 public:
  LogMessageQueue(size_t capacity);

  void Push(fuchsia::logger::LogMessage log_message);
  fuchsia::logger::LogMessage Pop();

 private:
  std::deque<fuchsia::logger::LogMessage> messages_;

  const size_t capacity_;

  std::condition_variable cv_;
  std::mutex mtx_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_UTILS_LOG_MESSAGE_QUEUE_H_
