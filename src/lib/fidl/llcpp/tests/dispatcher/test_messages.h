// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_LLCPP_TESTS_DISPATCHER_TEST_MESSAGES_H_
#define SRC_LIB_FIDL_LLCPP_TESTS_DISPATCHER_TEST_MESSAGES_H_

#include <lib/fidl/cpp/wire/message.h>

#include <cstdint>

namespace fidl_testing {

constexpr uint64_t kTestOrdinal = 0x1234567812345678ULL;

// |GoodMessage| is a helper to create a valid FIDL transactional message.
class GoodMessage {
 public:
  GoodMessage() {
    fidl::InitTxnHeader(&content_, 0, kTestOrdinal, fidl::MessageDynamicFlags::kStrictMethod);
  }

  fidl::OutgoingMessage message() {
    fidl_outgoing_msg_t c_msg = {
        .type = FIDL_OUTGOING_MSG_TYPE_BYTE,
        .byte =
            {
                .bytes = reinterpret_cast<uint8_t*>(&content_),
                .num_bytes = sizeof(content_),
            },
    };
    return fidl::OutgoingMessage::FromEncodedCMessage(&c_msg);
  }

  const fidl_type_t* type() const { return nullptr; }

 private:
  FIDL_ALIGNDECL fidl_message_header_t content_ = {};
};

}  // namespace fidl_testing

#endif  // SRC_LIB_FIDL_LLCPP_TESTS_DISPATCHER_TEST_MESSAGES_H_
