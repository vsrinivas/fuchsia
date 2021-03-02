// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "mmu.h"

#include <stdlib.h>

#include <gtest/gtest.h>

namespace page_table::x86 {

TEST(IsCanonicalVaddr, Simple) {
  EXPECT_TRUE(IsCanonicalVaddr(Vaddr(0x0000'0000'0000'0000)));
  EXPECT_TRUE(IsCanonicalVaddr(Vaddr(0x0000'7fff'ffff'ffff)));
  EXPECT_TRUE(IsCanonicalVaddr(Vaddr(0xffff'8000'0000'0000)));
  EXPECT_TRUE(IsCanonicalVaddr(Vaddr(0xffff'ffff'ffff'ffff)));

  EXPECT_FALSE(IsCanonicalVaddr(Vaddr(0x0000'8000'0000'0000)));
  EXPECT_FALSE(IsCanonicalVaddr(Vaddr(0x0000'ffff'ffff'ffff)));
  EXPECT_FALSE(IsCanonicalVaddr(Vaddr(0x0001'0000'0000'0000)));
  EXPECT_FALSE(IsCanonicalVaddr(Vaddr(0x0001'ffff'ffff'ffff)));
  EXPECT_FALSE(IsCanonicalVaddr(Vaddr(0x8000'0000'0000'0000)));
  EXPECT_FALSE(IsCanonicalVaddr(Vaddr(0xffff'0000'0000'0000)));
}

}  // namespace page_table::x86
