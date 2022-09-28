// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_IPC_MESSAGE_READER_H_
#define SRC_DEVELOPER_DEBUG_IPC_MESSAGE_READER_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/shared/serialization.h"

namespace debug_ipc {

class MessageReader : public Serializer {
 public:
  explicit MessageReader(std::vector<char> message) : message_(std::move(message)) {}

  bool has_error() const { return has_error_; }

  // Returns the number of bytes available still to read.
  size_t remaining() const { return message_.size() - offset_; }
  size_t message_size() const { return message_.size(); }

  // Implement |Serializer|.
  uint32_t GetVersion() const override { return kProtocolVersion; }
  // Although it's called "SerializeBytes", it's actually "DeserializeBytes".
  void SerializeBytes(void* data, uint32_t len) override;

 private:
  const std::vector<char> message_;

  size_t offset_ = 0;  // Current read offset.

  bool has_error_ = false;
};

// Helper functions to deserialize bytes into messages. Returns true if succeeds.
#define FN(msg_type)                                                                              \
  bool Deserialize(std::vector<char> data, msg_type##Request* request, uint32_t* transaction_id); \
  bool Deserialize(std::vector<char> data, msg_type##Reply* reply, uint32_t* transaction_id);

FOR_EACH_REQUEST_TYPE(FN)
#undef FN

#define FN(msg_name, msg_type) bool Deserialize##msg_name(std::vector<char> data, msg_type* notify);

FOR_EACH_NOTIFICATION_TYPE(FN)
#undef FN

}  // namespace debug_ipc

#endif  // SRC_DEVELOPER_DEBUG_IPC_MESSAGE_READER_H_
