// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/ipc/message_writer.h"

#include <string.h>

#include <cstdint>
#include <type_traits>

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

namespace {

template <typename MsgType>
auto IsSupported(uint32_t version) -> decltype(MsgType::kSupportedSinceVersion, true) {
  return version >= MsgType::kSupportedSinceVersion;
}

template <typename>
bool IsSupported(...) {
  return true;
}

}  // namespace

#define FN(msg_type)                                                                     \
  std::vector<char> Serialize(const msg_type##Request& request, uint32_t transaction_id, \
                              uint32_t version) {                                        \
    if (!IsSupported<msg_type##Request>(version)) {                                      \
      return {};                                                                         \
    }                                                                                    \
    MsgHeader header{0, MsgHeader::Type::k##msg_type, transaction_id};                   \
    MessageWriter writer(version, sizeof(header) + sizeof(request));                     \
    writer | header | const_cast<msg_type##Request&>(request);                           \
    return writer.MessageComplete();                                                     \
  }                                                                                      \
  std::vector<char> Serialize(const msg_type##Reply& reply, uint32_t transaction_id,     \
                              uint32_t version) {                                        \
    MsgHeader header{0, MsgHeader::Type::k##msg_type, transaction_id};                   \
    MessageWriter writer(version, sizeof(header) + sizeof(reply));                       \
    writer | header | const_cast<msg_type##Reply&>(reply);                               \
    return writer.MessageComplete();                                                     \
  }

FOR_EACH_REQUEST_TYPE(FN)
#undef FN

#define FN(msg_type)                                                      \
  std::vector<char> Serialize(const msg_type& notify, uint32_t version) { \
    if (!IsSupported<msg_type>(version)) {                                \
      return {};                                                          \
    }                                                                     \
    MsgHeader header{0, MsgHeader::Type::k##msg_type, 0};                 \
    MessageWriter writer(version, sizeof(header) + sizeof(notify));       \
    writer | header | const_cast<msg_type&>(notify);                      \
    return writer.MessageComplete();                                      \
  }

FOR_EACH_NOTIFICATION_TYPE(FN)
#undef FN

}  // namespace debug_ipc
