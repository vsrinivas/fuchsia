// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma.h"
#include "gtest/gtest.h"

#define BATCH_SIZE (8192 * sizeof(uint32_t))
#define BUF_SIZE 4096
#define BUF_ALIGN 0

TEST(MagmaBuffer, RelocationManagement)
{
    int32_t ret;
    auto connection = magma_bufmgr_gem_init(0xdeadbeef, BATCH_SIZE);
    ASSERT_NE(connection, nullptr);
    auto buf0 = magma_bo_alloc(connection, "test buf 0", BUF_SIZE, BUF_ALIGN);
    ASSERT_NE(buf0, nullptr);
    auto buf1 = magma_bo_alloc(connection, "test buf 1", BUF_SIZE, BUF_ALIGN);
    ASSERT_NE(buf1, nullptr);

    EXPECT_EQ(magma_gem_bo_get_reloc_count(buf0), 0);
    EXPECT_EQ(magma_gem_bo_get_reloc_count(buf1), 0);

    int32_t num_relocs = 10;

    for (int32_t i = 0; i < num_relocs; i++) {
        ret = magma_bo_emit_reloc(buf0, 0, buf1, 0, 0, 0);
        ASSERT_EQ(ret, 0);
        EXPECT_EQ(magma_gem_bo_get_reloc_count(buf0), i + 1);
        EXPECT_EQ(magma_gem_bo_get_reloc_count(buf1), 0);
    }

    magma_gem_bo_clear_relocs(buf0, num_relocs / 2);
    EXPECT_EQ(magma_gem_bo_get_reloc_count(buf0), num_relocs - (num_relocs / 2));

    magma_gem_bo_clear_relocs(buf0, 0);
    EXPECT_EQ(magma_gem_bo_get_reloc_count(buf0), 0);
}