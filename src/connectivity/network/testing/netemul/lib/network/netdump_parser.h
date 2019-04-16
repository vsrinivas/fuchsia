// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_NETDUMP_PARSER_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_NETDUMP_PARSER_H_

#include <string>
#include <vector>

namespace netemul {
namespace testing {

// Simple parser of net dumps used for testing.
class NetDumpParser {
 public:
  struct ParsedPacket {
    const uint8_t* data;
    size_t len;
    uint32_t interface;
  };

  // Parses a net dump pointed to by data and extracts
  // interface information and packet data.
  bool Parse(const uint8_t* data, size_t len);

  const std::vector<std::string>& interfaces() const { return interfaces_; }

  const std::vector<ParsedPacket>& packets() const { return packets_; }

 private:
  std::vector<std::string> interfaces_;
  std::vector<ParsedPacket> packets_;
};

}  // namespace testing
}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_NETDUMP_PARSER_H_
