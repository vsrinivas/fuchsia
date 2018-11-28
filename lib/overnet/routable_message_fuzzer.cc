// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing_header.h"

using namespace overnet;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Parse data as a header.
  auto status = RoutingHeader::Parse(&data, data + size, NodeId(1), NodeId(2));
  if (status.is_ok()) {
    // If parsed ok: rewrite, between different endpoints.
    RoutingHeader::Writer writer(status.get(), NodeId(3), NodeId(4));
    std::vector<uint8_t> encoded;
    encoded.resize(writer.wire_length());
    uint8_t* new_data = encoded.data();
    auto end = writer.Write(new_data);
    assert(end - new_data == writer.wire_length());
    // And re-parse.
    auto status2 = RoutingHeader::Parse(const_cast<const uint8_t**>(&new_data),
                                        new_data + writer.wire_length(),
                                        NodeId(4), NodeId(3));
    assert(new_data == encoded.data() + encoded.size());
    // Should parse ok, and get the same result.
    assert(status2.is_ok());
    assert(*status == *status2);
  }
  return 0;  // Non-zero return values are reserved for future use.
}
