// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hwreg/asm.h>

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

}  // namespace

int main(int argc, char** argv) {
  return hwreg::AsmHeader()
      .Register<TestReg32>("TR32_")
      .Macro("TR32_FIELD1_VALUE", 1234u)
      .Main(argc, argv);
}
