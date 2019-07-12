// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "magma_util/macros.h"
#include "magma_util/register_bitfields.h"

TEST(RegisterBitfields, Field1) {
  class TestReg : public magma::RegisterBase {
   public:
    DEF_FIELD(31, 0, field);
    static auto Get() { return magma::RegisterAddr<TestReg>(0); }
  };

  constexpr uint32_t val = 0xdeadbeef;
  EXPECT_EQ(val, TestReg::Get().FromValue(val).reg_value());
  EXPECT_EQ(val, TestReg::Get().FromValue(val).field().get());
}

TEST(RegisterBitfields, Field2) {
  class TestReg : public magma::RegisterBase {
   public:
    DEF_BIT(0, field);
    static auto Get() { return magma::RegisterAddr<TestReg>(0); }
  };

  constexpr uint32_t val = 0xdeadbeef;
  EXPECT_EQ(val, TestReg::Get().FromValue(val).reg_value());
  EXPECT_EQ(0x1u, TestReg::Get().FromValue(val).field().get());
}

TEST(RegisterBitfields, Field3) {
  class TestReg : public magma::RegisterBase {
   public:
    DEF_FIELD(1, 0, field);
    static auto Get() { return magma::RegisterAddr<TestReg>(0); }
  };

  constexpr uint32_t val = 0xdeadbeef;
  EXPECT_EQ(val, TestReg::Get().FromValue(val).reg_value());
  EXPECT_EQ(0x3u, TestReg::Get().FromValue(val).field().get());
}
