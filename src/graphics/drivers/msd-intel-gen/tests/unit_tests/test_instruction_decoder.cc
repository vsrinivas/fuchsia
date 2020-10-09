// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "instruction_decoder.h"

TEST(InstructionDecoder, DecodeSuccess) {
  InstructionDecoder::Id id;
  uint32_t dword_count = 0;
  constexpr uint32_t kInstruction = InstructionDecoder::LOAD_REGISTER_IMM << 16;
  EXPECT_TRUE(InstructionDecoder::Decode(kInstruction, &id, &dword_count));
  EXPECT_EQ(3u, dword_count);
}

TEST(InstructionDecoder, DecodeFail) {
  InstructionDecoder::Id id;
  uint32_t dword_count = 0;
  constexpr uint32_t kInstruction = 0xFFFF << 16;
  EXPECT_FALSE(InstructionDecoder::Decode(kInstruction, &id, &dword_count));
}
