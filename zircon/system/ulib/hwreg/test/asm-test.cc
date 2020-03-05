// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <hwreg/asm.h>
#include <zxtest/zxtest.h>

namespace {

class TestReg32 : public hwreg::RegisterBase<TestReg32, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_FIELD(30, 12, field1);
  DEF_BIT(11, field2);
  DEF_RSVDZ_FIELD(10, 5);
  DEF_FIELD(4, 3, field3);
  DEF_RSVDZ_BIT(2);
  DEF_RSVDZ_BIT(1);
  DEF_FIELD(0, 0, field4);

  static auto Get() { return hwreg::RegisterAddr<TestReg32>(0); }
};

TEST(AsmHeader, Output) {
  std::string contents = hwreg::AsmHeader()
                             .Register<TestReg32>("TR32_")
                             .Macro("TR32_FIELD1_VALUE", 1234u)
                             .Output("test/reg32.h");
  EXPECT_STR_EQ(R"(// This file is generated.  DO NOT EDIT!

#ifndef _TEST_REG32_H_
#define _TEST_REG32_H_ 1

#define TR32_FIELD1 0x7ffff000
#define TR32_FIELD2 0x800
#define TR32_FIELD2_BIT 11
#define TR32_FIELD3 0x18
#define TR32_FIELD4 0x1
#define TR32_FIELD4_BIT 0
#define TR32_RSVDZ 0x7e6
#define TR32_UNKNOWN 0x80000000
#define TR32_FIELD1_VALUE 0x4d2

#endif  // _TEST_REG32_H_
)",
                contents);
}

}  // namespace
