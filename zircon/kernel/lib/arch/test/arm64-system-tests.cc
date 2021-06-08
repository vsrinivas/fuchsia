// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/arch/arm64/system.h>

#include <algorithm>
#include <vector>

#include <gtest/gtest.h>

namespace {

using arch::ArmMemoryAttrIndirectionRegister;

bool ExpectAttrsConsistent(ArmMemoryAttrIndirectionRegister reg) {
  EXPECT_EQ(reg.attr0(), reg.GetAttribute(0));
  EXPECT_EQ(reg.attr1(), reg.GetAttribute(1));
  EXPECT_EQ(reg.attr2(), reg.GetAttribute(2));
  EXPECT_EQ(reg.attr3(), reg.GetAttribute(3));
  EXPECT_EQ(reg.attr4(), reg.GetAttribute(4));
  EXPECT_EQ(reg.attr5(), reg.GetAttribute(5));
  EXPECT_EQ(reg.attr6(), reg.GetAttribute(6));
  EXPECT_EQ(reg.attr7(), reg.GetAttribute(7));
  return true;
}

TEST(Arm64System, MairGetSetAttribute) {
  arch::ArmMemoryAttrIndirectionRegister val{};

  // Ensure everything consistent to begin with.
  EXPECT_EQ(val.reg_value(), 0u);
  ExpectAttrsConsistent(val);

  // Set some attributes by the setters. Ensure everything is consistent.
  val.set_attr0(arch::ArmMemoryAttribute::kNormalCached);
  ExpectAttrsConsistent(val);
  val.set_attr3(arch::ArmMemoryAttribute::kDevice_nGnRE);
  ExpectAttrsConsistent(val);
  val.set_attr7(arch::ArmMemoryAttribute::kNormalUncached);
  ExpectAttrsConsistent(val);

  // Set some attributes using SetAttribute. Ensure everything is consistent.
  val.SetAttribute(0, arch::ArmMemoryAttribute::kNormalUncached);
  ExpectAttrsConsistent(val);
  val.SetAttribute(3, arch::ArmMemoryAttribute::kNormalCached);
  ExpectAttrsConsistent(val);
  val.SetAttribute(7, arch::ArmMemoryAttribute::kDevice_nGnRE);
  ExpectAttrsConsistent(val);
}

}  // namespace
