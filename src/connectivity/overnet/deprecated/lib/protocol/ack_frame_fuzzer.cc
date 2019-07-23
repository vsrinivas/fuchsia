// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/deprecated/lib/protocol/ack_frame.h"

using namespace overnet;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  auto input = Slice::FromCopiedBuffer(data, size);
  // Parse data as an ack frame.
  auto status = AckFrame::Parse(input);
  if (status.is_ok()) {
    if (status->ack_to_seq() < 100000) {
      for (auto n : status->nack_seqs()) {
        [](auto) {}(n);
      }
    }
    // If parsed ok: rewrite.
    Slice written = Slice::FromWriters(AckFrame::Writer(status.get()));
    // Should get the equivalent set of bytes.
    auto status2 = AckFrame::Parse(written);
    assert(status2.is_ok());
    assert(*status == *status2);
  }
  return 0;  // Non-zero return values are reserved for future use.
}
