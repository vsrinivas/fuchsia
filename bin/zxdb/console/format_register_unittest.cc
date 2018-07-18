// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/format_register.cc"
#include "gtest/gtest.h"

namespace zxdb {

using namespace debug_ipc;

namespace {

std::vector<uint8_t> CreateData(size_t length) {
  std::vector<uint8_t> data;
  data.reserve(length);
  // So that we get the number backwards (0x0102...).
  uint8_t base = length;
  for (size_t i = 0; i < length; i++) {
    data.emplace_back(base - i);
  }
  return data;
}

debug_ipc::Register CreateRegister(RegisterID id,
                                   size_t length) {
  debug_ipc::Register reg;
  reg.id = id;
  reg.data = CreateData(length);
  return reg;
}

void GetCategories(RegisterSet* registers) {
  std::vector<debug_ipc::RegisterCategory> categories;

  RegisterCategory cat1;
  cat1.type = RegisterCategory::Type::kGeneral;
  cat1.registers.push_back(CreateRegister(RegisterID::ARMv8_lr, 1));
  cat1.registers.push_back(CreateRegister(RegisterID::ARMv8_pc, 2));
  cat1.registers.push_back(CreateRegister(RegisterID::ARMv8_sp, 4));
  cat1.registers.push_back(CreateRegister(RegisterID::ARMv8_cpsr, 8));
  categories.push_back(cat1);

  // Sanity check
  ASSERT_EQ(*(uint8_t*)&(cat1.registers[0].data[0]), 0x01u);
  ASSERT_EQ(*(uint16_t*)&(cat1.registers[1].data[0]), 0x0102u);
  ASSERT_EQ(*(uint32_t*)&(cat1.registers[2].data[0]), 0x01020304u);
  ASSERT_EQ(*(uint64_t*)&(cat1.registers[3].data[0]), 0x0102030405060708u);

  RegisterCategory cat2;
  cat2.type = RegisterCategory::Type::kVector;
  cat2.registers.push_back(CreateRegister(RegisterID::ARMv8_x0, 1));
  cat2.registers.push_back(CreateRegister(RegisterID::ARMv8_x1, 2));
  cat2.registers.push_back(CreateRegister(RegisterID::ARMv8_x2, 4));
  cat2.registers.push_back(CreateRegister(RegisterID::ARMv8_x3, 8));
  cat2.registers.push_back(CreateRegister(RegisterID::ARMv8_x4, 16));
  categories.push_back(cat2);

  RegisterSet regs(std::move(categories));
  *registers = std::move(regs);
}

}   // namespace

TEST(FormatRegisters, GeneralRegisters) {
  RegisterSet registers;
  GetCategories(&registers);
  OutputBuffer out;
  Err err = FormatRegisters(registers, "", &out);

  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ("General Purpose Registers\n"
            "Name Size               Value\n"
            "lr      1            00000001\n"
            "pc      2            00000102\n"
            "sp      4            01020304\n"
            "cpsr    8   01020304 05060708\n"
            "\n",
            out.AsString());
}

TEST(FormatRegisters, VectorRegisters) {
  RegisterSet registers;
  GetCategories(&registers);
  OutputBuffer out;
  Err err = FormatRegisters(registers, "", &out,
                            {RegisterCategory::Type::kVector});

  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ("Vector Registers\n"
            "Name Size                                 Value\n"
            "x0      1                              00000001\n"
            "x1      2                              00000102\n"
            "x2      4                              01020304\n"
            "x3      8                     01020304 05060708\n"
            "x4     10   01020304 05060708 090a0b0c 0d0e0f10\n"
            "\n",
            out.AsString());
}

TEST(FormatRegisters, AllRegisters) {
  RegisterSet registers;
  GetCategories(&registers);
  OutputBuffer out;
  Err err = FormatRegisters(registers, "", &out, {});

  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ("General Purpose Registers\n"
            "Name Size               Value\n"
            "lr      1            00000001\n"
            "pc      2            00000102\n"
            "sp      4            01020304\n"
            "cpsr    8   01020304 05060708\n"
            "\n"
            "Vector Registers\n"
            "Name Size                                 Value\n"
            "x0      1                              00000001\n"
            "x1      2                              00000102\n"
            "x2      4                              01020304\n"
            "x3      8                     01020304 05060708\n"
            "x4     10   01020304 05060708 090a0b0c 0d0e0f10\n"
            "\n",
            out.AsString());
}

TEST(FormatRegisters, OneRegister) {
  RegisterSet registers;
  GetCategories(&registers);
  OutputBuffer out;
  Err err = FormatRegisters(registers, "x3", &out, {});

  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ("Vector Registers\n"
            "Name Size               Value\n"
            "x3      8   01020304 05060708\n"
            "\n",
            out.AsString());
}

TEST(FormatRegisters, CannotFindRegister) {
  RegisterSet registers;
  GetCategories(&registers);
  OutputBuffer out;
  Err err = FormatRegisters(registers, "W0", &out);

  ASSERT_TRUE(err.has_error());
  EXPECT_EQ(err.msg(), "Unknown register \"W0\"");
}

}   // namespace zxdb
