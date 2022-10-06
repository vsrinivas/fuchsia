// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/ipc/message_reader.h"

#include <string.h>

#include <cstdint>

#include "src/developer/debug/ipc/protocol.h"

namespace debug_ipc {

void MessageReader::SerializeBytes(void* data, uint32_t len) {
  if (has_error_) {
    return;
  }
  if (message_.size() - offset_ < len) {
    has_error_ = true;
  } else {
    memcpy(data, &message_[offset_], len);
    offset_ += len;
  }
}

#define FN(msg_type)                                                                             \
  bool Deserialize(std::vector<char> data, msg_type##Request* request, uint32_t* transaction_id, \
                   uint32_t version) {                                                           \
    MessageReader reader(std::move(data), version);                                              \
    MsgHeader header;                                                                            \
    reader | header | *request;                                                                  \
    *transaction_id = header.transaction_id;                                                     \
    return !reader.has_error();                                                                  \
  }                                                                                              \
  bool Deserialize(std::vector<char> data, msg_type##Reply* reply, uint32_t* transaction_id,     \
                   uint32_t version) {                                                           \
    MessageReader reader(std::move(data), version);                                              \
    MsgHeader header;                                                                            \
    reader | header | *reply;                                                                    \
    *transaction_id = header.transaction_id;                                                     \
    return !reader.has_error();                                                                  \
  }

FOR_EACH_REQUEST_TYPE(FN)
#undef FN

#define FN(msg_type)                                                             \
  bool Deserialize(std::vector<char> data, msg_type* notify, uint32_t version) { \
    MessageReader reader(std::move(data), version);                              \
    MsgHeader header;                                                            \
    reader | header | *notify;                                                   \
    return !reader.has_error();                                                  \
  }

FOR_EACH_NOTIFICATION_TYPE(FN)
#undef FN

}  // namespace debug_ipc
