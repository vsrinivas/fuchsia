// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/system_log_recorder/log_message_store.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/utils/log_format.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {
namespace {

const std::string kDroppedFormatStr = "!!! DROPPED %lu MESSAGES !!!\n";

std::string MakeRepeatedWarning(const size_t message_count) {
  if (message_count == 2) {
    return kRepeatedOnceFormatStr;
  } else {
    return fxl::StringPrintf(kRepeatedFormatStr, message_count - 1);
  }
}

}  // namespace

LogMessageStore::LogMessageStore(size_t max_block_capacity_bytes, size_t max_buffer_capacity_bytes,
                                 std::unique_ptr<Encoder> encoder)
    : mtx_(),
      buffer_(),
      buffer_stats_(max_buffer_capacity_bytes),
      block_stats_(max_block_capacity_bytes),
      encoder_(std::move(encoder)) {
  FX_CHECK(max_block_capacity_bytes >= max_buffer_capacity_bytes);
}

void LogMessageStore::AddToBuffer(const std::string& str) {
  const std::string encoded = encoder_->Encode(str);
  buffer_.push_back(encoded);
  block_stats_.Use(encoded.size());
  buffer_stats_.Use(encoded.size());
}

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
    // We always add the repeated message to the buffer, even if it means going over bound as we
    // control its (small) size.
    AddToBuffer(repeated_msg);
  }
  last_pushed_message_count_ = 0;

  // 3. Early return on full buffer.
  if (buffer_stats_.IsFull()) {
    ++num_messages_dropped_;
    return false;
  }

  // 4. Serialize incoming message.
  const std::string str = Format(log);

  // 5. Push the incoming message if below the limit, otherwise drop it.
  if (buffer_stats_.CanUse(str.size())) {
    AddToBuffer(str);
    last_pushed_message_ = log.msg;
    last_pushed_message_count_ = 1;
    return true;
  } else {
    // We will drop the rest of the incoming messages until the next Consume(). This avoids trying
    // to squeeze in a shorter message that will wrongfully appear before the DROPPED message.
    buffer_stats_.MakeFull();
    ++num_messages_dropped_;
    return false;
  }
}

std::string LogMessageStore::Consume(bool* end_of_block) {
  FX_CHECK(end_of_block != nullptr);
  TRACE_DURATION("feedback:io", "LogMessageStore::Consume");

  std::lock_guard<std::mutex> lk(mtx_);

  // Optionally log whether the last message was repeated.
  if (last_pushed_message_count_ > 1) {
    AddToBuffer(MakeRepeatedWarning(last_pushed_message_count_));
    last_pushed_message_count_ = 1;
  }

  // Optionally log whether some messages were dropped.
  if (num_messages_dropped_ > 0) {
    AddToBuffer(fxl::StringPrintf(kDroppedFormatStr.c_str(), num_messages_dropped_));
    last_pushed_message_ = "";
    last_pushed_message_count_ = 0;
  }

  // We assume all messages end with a newline character.
  std::string str = fxl::JoinStrings(buffer_);

  buffer_.clear();
  buffer_stats_.Reset();
  num_messages_dropped_ = 0;

  // Reset the encoder at the end of a block.
  if (block_stats_.IsFull()) {
    block_stats_.Reset();
    encoder_->Reset();
    // We reset the last message pushed and its count so that we don't have a block starting with a
    // repeated message without the actual message.
    last_pushed_message_ = "";
    last_pushed_message_count_ = 0;
    *end_of_block = true;
  } else {
    *end_of_block = false;
  }

  return str;
}

}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics
