// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_TESTS_DISPATCHER_TEST_MESSAGES_H_
#define SRC_LIB_FIDL_CPP_TESTS_DISPATCHER_TEST_MESSAGES_H_

#include <lib/fidl/cpp/message.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/message.h>

#include <cstdint>

constexpr uint64_t kTestOrdinal = 0x1234567812345678;

// |GoodMessage| is a helper to create a valid FIDL transactional message.
class GoodMessage {
 public:
  GoodMessage() { fidl_init_txn_header(&content_, 0, kTestOrdinal); }

  fidl::HLCPPOutgoingMessage message() {
    return fidl::HLCPPOutgoingMessage(
        fidl::BytePart(reinterpret_cast<uint8_t*>(&content_), sizeof(content_), sizeof(content_)),
        fidl::HandleDispositionPart());
  }

  const fidl_type_t* type() const { return &::fidl::_llcpp_coding_AnyZeroArgMessageTable; }

 private:
  FIDL_ALIGNDECL fidl_message_header_t content_ = {};
};

// |BadMessage| is a helper to create an invalid FIDL transactional message.
//
// Specifically, the message has more bytes than expected (zero arg request).
class BadMessage {
 public:
  fidl::HLCPPOutgoingMessage message() {
    return fidl::HLCPPOutgoingMessage(
        fidl::BytePart(too_large_.data(), too_large_.size(), too_large_.size()),
        fidl::HandleDispositionPart());
  }

  const fidl_type_t* type() const { return &::fidl::_llcpp_coding_AnyZeroArgMessageTable; }

 private:
  FIDL_ALIGNDECL std::array<uint8_t, sizeof(fidl_message_header_t) * 2> too_large_;
};

#endif  // SRC_LIB_FIDL_CPP_TESTS_DISPATCHER_TEST_MESSAGES_H_
