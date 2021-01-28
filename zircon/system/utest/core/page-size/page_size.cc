// Copyright 2021 The Fuchsia Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/limits.h>
#include <zircon/syscalls.h>

#include <zxtest/zxtest.h>

namespace {

// Ensure the page size is set.
TEST(PageSize, NotZero) { EXPECT_NE(0u, zx_system_get_page_size()); }

// Validate reported page size is correctly a power of two.
TEST(PageSize, PowerOfTwo) {
  const long page_size = zx_system_get_page_size();
  const long page_shift = __builtin_ctzl(page_size);
  EXPECT_EQ(page_size, 1ul << page_shift);
}

TEST(PageSize, ArchMinPageSize) { EXPECT_TRUE(ZX_MIN_PAGE_SIZE <= zx_system_get_page_size()); }
TEST(PageSize, ArchMaxPageSize) { EXPECT_TRUE(ZX_MAX_PAGE_SIZE >= zx_system_get_page_size()); }

// Currently we only support precisely 4k pages. Once we support other page sizes this test should
// be changed or deleted.
TEST(PageSize, Only4K) { EXPECT_EQ(4096, zx_system_get_page_size()); }

}  // namespace
