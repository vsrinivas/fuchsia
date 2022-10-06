// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_IPC_MESSAGE_WRITER_H_
#define SRC_DEVELOPER_DEBUG_IPC_MESSAGE_WRITER_H_

#include <stdint.h>

#include <cstdint>
#include <vector>

#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/shared/serialization.h"

namespace debug_ipc {

// Provides a simple means to append to a dynamic buffer different types of
// data.
//
// The first 4 bytes of each message is the message size. It's assumed that
// these bytes will be explicitly written to. Normally a message will start
// with a struct which contains space for this explicitly.
class MessageWriter : public Serializer {
 public:
  // |initial_size| is a hint for the initial size of the message.
  MessageWriter(uint32_t version, size_t initial_size) : version_(version) {
    buffer_.reserve(initial_size);
  }

  size_t current_length() const { return buffer_.size(); }

  // Writes the size of the current buffer to the first 4 bytes, and
  // destructively returns the buffer.
  std::vector<char> MessageComplete();

  // Implement |Serializer|.
  uint32_t GetVersion() const override { return version_; }
  void SerializeBytes(void* data, uint32_t len) override;

 private:
  uint32_t version_;
  std::vector<char> buffer_;
};

// Helper functions to serialize messages into bytes.
#define FN(msg_type)                                                                     \
  std::vector<char> Serialize(const msg_type##Request& request, uint32_t transaction_id, \
                              uint32_t version);                                         \
  std::vector<char> Serialize(const msg_type##Reply& reply, uint32_t transaction_id,     \
                              uint32_t version);

FOR_EACH_REQUEST_TYPE(FN)
#undef FN

#define FN(msg_type) std::vector<char> Serialize(const msg_type& notify, uint32_t version);

FOR_EACH_NOTIFICATION_TYPE(FN)
#undef FN

}  // namespace debug_ipc

#endif  // SRC_DEVELOPER_DEBUG_IPC_MESSAGE_WRITER_H_
