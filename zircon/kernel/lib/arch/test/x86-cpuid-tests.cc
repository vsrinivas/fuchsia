// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/arch/x86/cpuid.h>

#include <zxtest/zxtest.h>

namespace {

TEST(CpuidTests, Family) {
  {
    auto version =
        arch::CpuidVersionInfo::Get().FromValue(0).set_base_family(0xf).set_extended_family(0xf0);
    EXPECT_EQ(0xff, version.family());
  }

  // Extended family ID is ignored for other families.
  {
    auto version = arch::CpuidVersionInfo::Get()
                       .FromValue(0)
                       .set_base_family(0x6)
                       // Suppose this is garbage or an internal detail.
                       .set_extended_family(0xf0);
    EXPECT_EQ(0x06, version.family());
  }
}

TEST(CpuidTests, Model) {
  {
    auto version = arch::CpuidVersionInfo::Get()
                       .FromValue(0)
                       .set_base_family(0x6)
                       .set_base_model(0xa)
                       .set_extended_model(0xb);
    EXPECT_EQ(0xba, version.model());
  }

  {
    auto version = arch::CpuidVersionInfo::Get()
                       .FromValue(0)
                       .set_base_family(0xf)
                       .set_base_model(0xa)
                       .set_extended_model(0xb);
    EXPECT_EQ(0xba, version.model());
  }

  // Extended model ID is ignored for other families.
  {
    auto version = arch::CpuidVersionInfo::Get()
                       .FromValue(0)
                       .set_base_family(0x1)
                       .set_base_model(0xa)
                       // Suppose this is garbage or an internal detail.
                       .set_extended_model(0xf);
    EXPECT_EQ(0x0a, version.model());
  }
}

}  // namespace
