// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fxt/fields.h>

#include <gtest/gtest.h>

namespace {

TEST(Types, Set) {
  uint64_t value = 0;

  // Test setting fields.
  fxt::Field<0, 0>::Set(value, 1);
  fxt::Field<1, 1>::Set(value, 1);
  fxt::Field<2, 2>::Set(value, 1);
  fxt::Field<3, 3>::Set(value, 1);
  fxt::Field<4, 4>::Set(value, 1);
  fxt::Field<5, 5>::Set(value, 1);
  fxt::Field<6, 6>::Set(value, 1);
  fxt::Field<7, 7>::Set(value, 1);
  EXPECT_EQ(0xffULL, value);

  // Test updating fields.
  fxt::Field<4, 7>::Set(value, 0xa);
  EXPECT_EQ(0xafULL, value);

  // Test field overflow.
  fxt::Field<4, 7>::Set(value, 0xffff);
  EXPECT_EQ(0xffULL, value);
}

TEST(Types, Get) {
  uint64_t value = 0xff00aa55;

  EXPECT_EQ(0xff, (fxt::Field<24, 31>::Get<uint8_t>(value)));
  EXPECT_EQ(0x00, (fxt::Field<16, 23>::Get<uint8_t>(value)));
  EXPECT_EQ(0xaa, (fxt::Field<8, 15>::Get<uint8_t>(value)));
  EXPECT_EQ(0x55, (fxt::Field<0, 7>::Get<uint8_t>(value)));
}

TEST(Types, Make) {
  EXPECT_EQ(0xff000000ULL, (fxt::Field<24, 31>::Make(0xff)));
  EXPECT_EQ(0x00cc0000ULL, (fxt::Field<16, 23>::Make(0xcc)));
  EXPECT_EQ(0x0000aa00ULL, (fxt::Field<8, 15>::Make(0xaa)));
  EXPECT_EQ(0x00000055ULL, (fxt::Field<0, 7>::Make(0x55)));

  // Test field overflow.
  EXPECT_EQ(0x00000055ULL, (fxt::Field<0, 7>::Make(0xaa55)));
}

}  // namespace
