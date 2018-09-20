// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "introduction.h"
#include "gtest/gtest.h"

namespace overnet {
namespace introduction_test {

TEST(Introduction, Empty) {
  EXPECT_EQ(Slice::FromContainer({}), Introduction().Write(Border::None()));
}

TEST(Introduction, OneVal) {
  Introduction intro;
  intro[Introduction::Key::ServiceName] = Slice::FromStaticString("Hello!");
  EXPECT_EQ(Slice::FromStaticString("\1\6Hello!"), intro.Write(Border::None()));
  auto parsed = Introduction::Parse(intro.Write(Border::None()));
  ASSERT_TRUE(parsed.is_ok()) << parsed.AsStatus();
  EXPECT_EQ(Slice::FromStaticString("Hello!"),
            *(*parsed.get())[Introduction::Key::ServiceName]);
}

}  // namespace introduction_test
}  // namespace overnet
