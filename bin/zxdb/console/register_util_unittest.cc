// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/register_util.cc"
#include "gtest/gtest.h"

namespace zxdb {

using namespace debug_ipc;

namespace {

std::vector<RegisterCategory> GetCategories() {
  std::vector<debug_ipc::RegisterCategory> categories;

  RegisterCategory cat1;
  cat1.type = RegisterCategory::Type::kGeneral;
  cat1.registers.push_back({"EAX", 0xf000});
  cat1.registers.push_back({"EBX", 0xf001});
  cat1.registers.push_back({"ECX", 0xf002});
  categories.push_back(cat1);

  RegisterCategory cat2;
  cat2.type = RegisterCategory::Type::kVector;
  cat2.registers.push_back({"XMM0", 0xf003});
  cat2.registers.push_back({"XMM1", 0xf004});
  cat2.registers.push_back({"XMM2", 0xf005});
  cat2.registers.push_back({"YMM0", 0xf006});
  cat2.registers.push_back({"YMM1", 0xf007});
  cat2.registers.push_back({"YMM2", 0xf008});
  cat2.registers.push_back({"YMM3", 0xf009});
  cat2.registers.push_back({"ZMM0", 0xf006});
  cat2.registers.push_back({"ZMM1", 0xf007});
  cat2.registers.push_back({"ZMM2", 0xf008});
  cat2.registers.push_back({"ZMM3", 0xf009});
  cat2.registers.push_back({"ZMM4", 0xf010});
  categories.push_back(cat2);

  return categories;
}

}   // namespace

TEST(FormatRegisters, AllRegisters) {
  auto categories = GetCategories();
  OutputBuffer out;
  Err err = FormatRegisters(categories, "", &out);

  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ("General Purpose Registers\n"
            "Name   Value\n"
            " EAX   0x000000000000f000\n"
            " EBX   0x000000000000f001\n"
            " ECX   0x000000000000f002\n"
            "\n"
            "Vector Registers\n"
            "Name   Value\n"
            "XMM0   0x000000000000f003\n"
            "XMM1   0x000000000000f004\n"
            "XMM2   0x000000000000f005\n"
            "YMM0   0x000000000000f006\n"
            "YMM1   0x000000000000f007\n"
            "YMM2   0x000000000000f008\n"
            "YMM3   0x000000000000f009\n"
            "ZMM0   0x000000000000f006\n"
            "ZMM1   0x000000000000f007\n"
            "ZMM2   0x000000000000f008\n"
            "ZMM3   0x000000000000f009\n"
            "ZMM4   0x000000000000f010\n"
            "\n",
            out.AsString());
}

TEST(FormatRegisters, OneRegister) {
  auto categories = GetCategories();
  OutputBuffer out;
  Err err = FormatRegisters(categories, "ZMM2", &out);

  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ("Vector Registers\n"
            "Name   Value\n"
            "ZMM2   0x000000000000f008\n"
            "\n",
            out.AsString());
}

TEST(FormatRegisters, CannotFindRegister) {
  auto categories = GetCategories();
  OutputBuffer out;
  Err err = FormatRegisters(categories, "W0", &out);

  ASSERT_TRUE(err.has_error());
  EXPECT_EQ(err.msg(), "Unknown register \"W0\"");
}

}   // namespace zxdb
