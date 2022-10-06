// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_IPC_MESSAGE_READER_H_
#define SRC_DEVELOPER_DEBUG_IPC_MESSAGE_READER_H_

#include <stdint.h>

#include <cstdint>
#include <string>
#include <vector>

#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/shared/serialization.h"

namespace debug_ipc {

class MessageReader : public Serializer {
 public:
  MessageReader(std::vector<char> message, uint32_t version)
      : message_(std::move(message)), version_(version) {}

  bool has_error() const { return has_error_; }

  // Returns the number of bytes available still to read.
  size_t remaining() const { return message_.size() - offset_; }
  size_t message_size() const { return message_.size(); }

  // Implement |Serializer|.
  uint32_t GetVersion() const override { return version_; }
  // Although it's called "SerializeBytes", it's actually "DeserializeBytes".
  void SerializeBytes(void* data, uint32_t len) override;

 private:
  const std::vector<char> message_;

  uint32_t version_ = 0;

  size_t offset_ = 0;  // Current read offset.

  bool has_error_ = false;
};

// Helper functions to deserialize bytes into messages. Returns true if succeeds.
#define FN(msg_type)                                                                             \
  bool Deserialize(std::vector<char> data, msg_type##Request* request, uint32_t* transaction_id, \
                   uint32_t version);                                                            \
  bool Deserialize(std::vector<char> data, msg_type##Reply* reply, uint32_t* transaction_id,     \
                   uint32_t version);

FOR_EACH_REQUEST_TYPE(FN)
#undef FN

#define FN(msg_type) bool Deserialize(std::vector<char> data, msg_type* notify, uint32_t version);

FOR_EACH_NOTIFICATION_TYPE(FN)
#undef FN

}  // namespace debug_ipc

#endif  // SRC_DEVELOPER_DEBUG_IPC_MESSAGE_READER_H_
