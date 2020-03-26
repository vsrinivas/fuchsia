// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/system_log_recorder/log_message_store.h"

#include <lib/trace/event.h>

#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace feedback {
namespace {

const std::string kDroppedFormatStr = "!!! DROPPED %lu LOG MESSAGES !!!\n";

}  // namespace

LogMessageStore::LogMessageStore(size_t max_capacity_bytes)
    : mtx_(),
      queue_(),
      max_capacity_bytes_(max_capacity_bytes),
      bytes_remaining_(max_capacity_bytes_) {}

bool LogMessageStore::Add(std::string message) {
  TRACE_DURATION("feedback:io", "LogMessageStore::Add");

  std::lock_guard<std::mutex> lk(mtx_);

  if (bytes_remaining_ >= message.size()) {
    bytes_remaining_ -= message.size();
    queue_.push_back(std::move(message));
    return true;
  } else {
    ++num_messages_dropped;
    return false;
  }
}

std::string LogMessageStore::Consume() {
  TRACE_DURATION("feedback:io", "LogMessageStore::Consume");

  std::lock_guard<std::mutex> lk(mtx_);

  // We assume all messages end with a newline character.
  std::string str = fxl::JoinStrings(queue_);

  if (num_messages_dropped > 0) {
    str += fxl::StringPrintf(kDroppedFormatStr.c_str(), num_messages_dropped);
  }

  queue_.clear();
  bytes_remaining_ = max_capacity_bytes_;
  num_messages_dropped = 0;

  return str;
}

}  // namespace feedback
