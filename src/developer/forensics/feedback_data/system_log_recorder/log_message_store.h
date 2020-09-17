// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_LOG_MESSAGE_STORE_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_LOG_MESSAGE_STORE_H_

#include <fuchsia/logger/cpp/fidl.h>

#include <deque>
#include <mutex>

#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/encoder.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {

// Thread-safe store of log messages.
//
// Buffer:
// The store has a buffer with limited capacity that is filled with successive Add() calls. This
// buffer is cleared when Consume() is called; returning the accumulated buffer as an encoded
// string.
//
// Block:
// When storing messages, the messages are first encoded. Encoded messages are then stored in
// finite blocks; these blocks have a specific size, and the information of an encoded message
// depends on the Block. Upon Consume, in addition to returning the buffer contents, a signal that
// notifies the end of block (after the buffer) is also sent.
//
// Note: Both the buffer and the block overcommit, i.e. if not full, the last message will be
// pushed entirely, even if it means going overbound.
class LogMessageStore {
 public:
  LogMessageStore(size_t max_block_capacity_bytes, size_t max_buffer_capacity_bytes,
                  std::unique_ptr<Encoder> encoder);

  // May add the encoded log message to the store:
  // * The message is dropped if the store has reached its maximum capacity, returning false.
  // * The message is omitted if it is the same one as the previous one in the store.
  bool Add(fuchsia::logger::LogMessage log);

  // Consumes the contents of the store as a string and sends a signal that notifies the end
  // of the block (after the returned string). Calling Consume will empty the store.
  std::string Consume(bool* end_of_block);

 private:
  class ContainerStats {
   public:
    ContainerStats(size_t capacity_in_bytes)
        : capacity_in_bytes_(capacity_in_bytes), bytes_remaining_(capacity_in_bytes){};
    // Reduces the free space in the container by |quantity|.
    void Use(size_t quantity) {
      // We allow overcommitting, but we cap |bytes_remaining_| at 0.
      bytes_remaining_ -= std::min(bytes_remaining_, quantity);
    }
    void MakeFull() { bytes_remaining_ = 0; }
    bool CanUse(size_t quantity) { return bytes_remaining_ >= quantity; }
    void Reset() { bytes_remaining_ = capacity_in_bytes_; }
    bool IsFull() { return bytes_remaining_ == 0; }

   private:
    size_t capacity_in_bytes_;
    size_t bytes_remaining_;
  };

  // Encodes the string, stores it in the buffer, and reduces the free space remaining for
  // the buffer and the block.
  void AddToBuffer(const std::string& str);

  std::mutex mtx_;
  std::deque<std::string> buffer_;

  ContainerStats buffer_stats_;
  ContainerStats block_stats_;

  size_t num_messages_dropped_ = 0;
  size_t last_pushed_message_count_ = 0;
  std::string last_pushed_message_;
  size_t repeat_buffer_count_ = 0;

  std::unique_ptr<Encoder> encoder_;
};

}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_LOG_MESSAGE_STORE_H_
