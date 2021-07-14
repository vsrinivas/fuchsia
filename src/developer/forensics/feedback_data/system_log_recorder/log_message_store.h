// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_LOG_MESSAGE_STORE_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_LOG_MESSAGE_STORE_H_

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/fpromise/result.h>

#include <deque>
#include <mutex>

#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/encoder.h"
#include "src/developer/forensics/utils/storage_size.h"

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
  LogMessageStore(StorageSize max_block_capacity, StorageSize max_buffer_capacity,
                  std::unique_ptr<Encoder> encoder);

  // May add the encoded log message to the store:
  // * The message is dropped if the store has reached its maximum capacity, returning false.
  // * The message is omitted if it is the same one as the previous one in the store.
  bool Add(::fpromise::result<fuchsia::logger::LogMessage, std::string> log);

  // |str| will be the final message in the consumed buffer, after the dropped and repeated
  // messages.
  void AppendToEnd(const std::string& str);

  // Consumes the contents of the store as a string and sends a signal that notifies the end
  // of the block (after the returned string). Calling Consume will empty the store.
  std::string Consume(bool* end_of_block);

  void TurnOnRateLimiting() { buffer_rate_limit_ = true; }

 private:
  class ContainerStats {
   public:
    explicit ContainerStats(const StorageSize capacity)
        : capacity_(capacity), remaining_(capacity_){};
    // Reduces the free space in the container by |quantity|.
    void Use(const StorageSize quantity) {
      // We allow overcommitting, but we cap |remaining_| at 0.
      remaining_ -= std::min(remaining_, quantity);
    }
    void MakeFull() { remaining_ = StorageSize::Bytes(0); }
    bool CanUse(const StorageSize quantity) { return remaining_ >= quantity; }
    void Reset() { remaining_ = capacity_; }
    bool IsFull() { return remaining_ == StorageSize::Bytes(0); }

   private:
    StorageSize capacity_;
    StorageSize remaining_;
  };

  // Encodes the string, stores it in the buffer, and reduces the free space remaining for
  // the buffer and the block.
  void AddToBuffer(const std::string& str);

  std::mutex mtx_;
  std::deque<std::string> buffer_;

  ContainerStats buffer_stats_;
  ContainerStats block_stats_;

  bool buffer_rate_limit_ = false;
  size_t num_messages_dropped_ = 0;
  size_t last_pushed_message_count_ = 0;
  std::string last_pushed_message_;
  size_t repeat_buffer_count_ = 0;
  std::optional<std::string> to_append_ = std::nullopt;

  std::unique_ptr<Encoder> encoder_;
};

}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_LOG_MESSAGE_STORE_H_
