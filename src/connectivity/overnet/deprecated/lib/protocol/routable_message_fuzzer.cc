// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/deprecated/lib/protocol/routable_message.h"

using namespace overnet;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Parse data as a header.
  auto status = RoutableMessage::Parse(Slice::FromCopiedBuffer(data, size), NodeId(1), NodeId(2));
  if (status.is_ok()) {
    // If parsed ok: rewrite, between different endpoints.
    Slice written = status->message.Write(NodeId(3), NodeId(4), status->payload);
    // And re-parse.
    auto status2 = RoutableMessage::Parse(written, NodeId(4), NodeId(3));
    // Should parse ok, and get the same result.
    assert(status2.is_ok());
    assert(status->message == status2->message);
    assert(status->payload == status2->payload);
  }
  return 0;  // Non-zero return values are reserved for future use.
}
