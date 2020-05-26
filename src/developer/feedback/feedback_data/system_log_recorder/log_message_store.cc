// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_data/system_log_recorder/log_message_store.h"

#include <lib/trace/event.h>

#include "src/developer/feedback/utils/log_format.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace feedback {
namespace {

const std::string kDroppedFormatStr = "!!! DROPPED %lu MESSAGES !!!\n";
const std::string kRepeatedOnceFormatStr = "!!! MESSAGE REPEATED 1 MORE TIME !!!\n";
const std::string kRepeatedFormatStr = "!!! MESSAGE REPEATED %lu MORE TIMES !!!\n";

std::string MakeRepeatedWarning(const size_t message_count) {
  if (message_count == 2) {
    return kRepeatedOnceFormatStr;
  } else {
    return fxl::StringPrintf(kRepeatedFormatStr.c_str(), message_count - 1);
  }
}

}  // namespace

LogMessageStore::LogMessageStore(size_t max_capacity_bytes)
    : mtx_(),
      queue_(),
      max_capacity_bytes_(max_capacity_bytes),
      bytes_remaining_(max_capacity_bytes_) {}

bool LogMessageStore::Add(fuchsia::logger::LogMessage log) {
  TRACE_DURATION("feedback:io", "LogMessageStore::Add");

  std::lock_guard<std::mutex> lk(mtx_);

  // 1. Early return if the incoming message is the same as last time.
  if (last_pushed_message_ == log.msg) {
    last_pushed_message_count_++;
    return true;
  }
  last_pushed_message_ = "";

  // 2. Push the repeated message if any.
  if (last_pushed_message_count_ > 1) {
    const std::string repeated_msg = MakeRepeatedWarning(last_pushed_message_count_);
    queue_.push_back(std::move(repeated_msg));
    // We allow the repeated message to go over bound as we control its (small) size.
    if (bytes_remaining_ < repeated_msg.size()) {
      bytes_remaining_ = 0;
    } else {
      bytes_remaining_ -= repeated_msg.size();
    }
  }
  last_pushed_message_count_ = 0;

  // 3. Early return on full buffer.
  if (bytes_remaining_ == 0) {
    ++num_messages_dropped_;
    return false;
  }

  // 4. Serialize incoming message.
  std::string str = Format(log);

  // 5. Push the incoming message if below the limit, otherwise drop it.
  if (str.size() <= bytes_remaining_) {
    bytes_remaining_ -= str.size();
    queue_.push_back(std::move(str));
    last_pushed_message_ = log.msg;
    last_pushed_message_count_ = 1;
    return true;
  } else {
    // We will drop the rest of the incoming messages until the next Consume(). This avoids trying
    // to squeeze in a shorter message that will wrongfully appear before the DROPPED message.
    bytes_remaining_ = 0;
    ++num_messages_dropped_;
    return false;
  }
}

std::string LogMessageStore::Consume() {
  TRACE_DURATION("feedback:io", "LogMessageStore::Consume");

  std::lock_guard<std::mutex> lk(mtx_);

  // We assume all messages end with a newline character.
  std::string str = fxl::JoinStrings(queue_);

  // Optionally log whether the last message was repeated.
  if (last_pushed_message_count_ > 1) {
    str += MakeRepeatedWarning(last_pushed_message_count_);
    last_pushed_message_count_ = 1;
  }

  // Optionally log whether some messages were dropped.
  if (num_messages_dropped_ > 0) {
    str += fxl::StringPrintf(kDroppedFormatStr.c_str(), num_messages_dropped_);
    last_pushed_message_ = "";
    last_pushed_message_count_ = 0;
  }

  queue_.clear();
  bytes_remaining_ = max_capacity_bytes_;
  num_messages_dropped_ = 0;

  return str;
}

}  // namespace feedback
