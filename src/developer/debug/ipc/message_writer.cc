// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/ipc/message_writer.h"

#include <string.h>

namespace debug_ipc {

void MessageWriter::SerializeBytes(void* data, uint32_t len) {
  const char* begin = static_cast<const char*>(data);
  const char* end = begin + len;
  buffer_.insert(buffer_.end(), begin, end);
}

std::vector<char> MessageWriter::MessageComplete() {
  uint32_t size = static_cast<uint32_t>(buffer_.size());
  memcpy(buffer_.data(), &size, sizeof(uint32_t));
  return std::move(buffer_);
}

#define FN(msg_type)                                                                       \
  std::vector<char> Serialize(const msg_type##Request& request, uint32_t transaction_id) { \
    MsgHeader header{0, MsgHeader::Type::k##msg_type, transaction_id};                     \
    MessageWriter writer(sizeof(header) + sizeof(request));                                \
    writer | header | const_cast<msg_type##Request&>(request);                             \
    return writer.MessageComplete();                                                       \
  }                                                                                        \
  std::vector<char> Serialize(const msg_type##Reply& reply, uint32_t transaction_id) {     \
    MsgHeader header{0, MsgHeader::Type::k##msg_type, transaction_id};                     \
    MessageWriter writer(sizeof(header) + sizeof(reply));                                  \
    writer | header | const_cast<msg_type##Reply&>(reply);                                 \
    return writer.MessageComplete();                                                       \
  }

FOR_EACH_REQUEST_TYPE(FN)
#undef FN

#define FN(msg_name, msg_type)                                    \
  std::vector<char> Serialize##msg_name(const msg_type& notify) { \
    MsgHeader header{0, MsgHeader::Type::k##msg_name, 0};         \
    MessageWriter writer(sizeof(header) + sizeof(notify));        \
    writer | header | const_cast<msg_type&>(notify);              \
    return writer.MessageComplete();                              \
  }

FOR_EACH_NOTIFICATION_TYPE(FN)
#undef FN

}  // namespace debug_ipc
