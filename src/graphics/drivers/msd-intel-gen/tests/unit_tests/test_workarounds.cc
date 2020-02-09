// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "workarounds.h"

class Buffer : public magma::InstructionWriter {
 public:
  void Write32(uint32_t value) { bytes_written_ += sizeof(value); }

  uint32_t bytes_written_ = 0;
};

TEST(Workarounds, Init) {
  Buffer buffer;
  EXPECT_TRUE(Workarounds::Init(&buffer, RENDER_COMMAND_STREAMER));
  EXPECT_EQ(buffer.bytes_written_, Workarounds::InstructionBytesRequired());
}
