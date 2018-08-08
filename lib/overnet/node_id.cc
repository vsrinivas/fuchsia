// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "node_id.h"
#include <iomanip>
#include <sstream>

namespace overnet {

std::ostream& operator<<(std::ostream& out, NodeId node_id) {
  return out << node_id.ToString();
}

std::string NodeId::ToString() const {
  std::ostringstream tmp;
  tmp << "[";
  tmp << std::hex << std::setfill('0') << std::setw(4);
  tmp << ((id_ >> 48) & 0xffff);
  tmp << "_";
  tmp << std::hex << std::setfill('0') << std::setw(4);
  tmp << ((id_ >> 32) & 0xffff);
  tmp << "_";
  tmp << std::hex << std::setfill('0') << std::setw(4);
  tmp << ((id_ >> 16) & 0xffff);
  tmp << "_";
  tmp << std::hex << std::setfill('0') << std::setw(4);
  tmp << (id_ & 0xffff);
  tmp << "]";
  return tmp.str();
}

StatusOr<NodeId> NodeId::FromString(const std::string& s) {
  auto ch = s.begin();
  const auto end = s.end();
  uint64_t node_value = 0;
  auto short_string = []() {
    return Status(StatusCode::INVALID_ARGUMENT, "String too short for node");
  };
  auto expected = [](char c, char got) {
    std::ostringstream tmp;
    tmp << "Expected '" << c << "' but got '" << got << "'";
    return Status(StatusCode::INVALID_ARGUMENT, tmp.str());
  };
  auto expect_eos = [&]() {
    if (ch == end) return Status::Ok();
    return Status(StatusCode::INVALID_ARGUMENT, "Expected end of string");
  };
  auto expect = [&](char c) {
    if (ch == end) return short_string();
    if (*ch != c) return expected(c, *ch);
    ++ch;
    return Status::Ok();
  };
  auto hex_digit = [&](int offset) {
    if (ch == end) return short_string();
    if (*ch >= '0' && *ch <= '9') {
      node_value |= static_cast<uint64_t>(static_cast<uint8_t>(*ch - '0'))
                    << offset;
      ++ch;
      return Status::Ok();
    }
    if (*ch >= 'a' && *ch <= 'f') {
      node_value |= static_cast<uint64_t>(static_cast<uint8_t>(*ch - 'a') + 10)
                    << offset;
      ++ch;
      return Status::Ok();
    }
    if (*ch >= 'A' && *ch <= 'F') {
      node_value |= static_cast<uint64_t>(static_cast<uint8_t>(*ch - 'A') + 10)
                    << offset;
      ++ch;
      return Status::Ok();
    }
    std::ostringstream tmp;
    tmp << "Expected a hex character but got '" << *ch << "'";
    return Status(StatusCode::INVALID_ARGUMENT, tmp.str());
  };
  auto hex_group = [&](int orig_shift) {
    return hex_digit(orig_shift + 12)
        .Then([&]() { return hex_digit(orig_shift + 8); })
        .Then([&]() { return hex_digit(orig_shift + 4); })
        .Then([&]() { return hex_digit(orig_shift); });
  };
  return expect('[')
      .Then([&]() { return hex_group(48); })
      .Then([&]() { return expect('_'); })
      .Then([&]() { return hex_group(32); })
      .Then([&]() { return expect('_'); })
      .Then([&]() { return hex_group(16); })
      .Then([&]() { return expect('_'); })
      .Then([&]() { return hex_group(0); })
      .Then([&]() { return expect(']'); })
      .Then([&]() { return expect_eos(); })
      .Then([&]() -> StatusOr<NodeId> { return NodeId(node_value); });
}

}  // namespace overnet
