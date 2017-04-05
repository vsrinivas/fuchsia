// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/iotxn.h>

#include <unittest/unittest.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static bool test_physmap_simple(void) {
    BEGIN_TEST;
    iotxn_t* txn;
    ASSERT_EQ(iotxn_alloc(&txn, 0, PAGE_SIZE), NO_ERROR, "");
    ASSERT_NONNULL(txn, "");
    iotxn_sg_t* sg;
    uint32_t sgl;
    ASSERT_EQ(iotxn_physmap(txn, &sg, &sgl), NO_ERROR, "");
    ASSERT_NONNULL(txn->sg, "");
    iotxn_release(txn);
    ASSERT_NONNULL(txn->sg, "returning txn to free list should not free txn->sg");
    END_TEST;
}

static bool test_pages_to_sg_simple(void) {
    BEGIN_TEST;
    mx_paddr_t paddr = PAGE_SIZE;
    iotxn_sg_t sg;
    uint32_t sg_len;
    iotxn_pages_to_sg(&paddr, &sg, 1, &sg_len);
    ASSERT_EQ(sg_len, 1u, "unexpected sg_len");
    ASSERT_EQ(sg.paddr, paddr, "unexpected address in sg entry");
    ASSERT_EQ(sg.length, (uint64_t)PAGE_SIZE, "unexpected length in sg entry");
    END_TEST;
}

static bool test_pages_to_sg_contiguous(void) {
    BEGIN_TEST;
    mx_paddr_t paddrs[2] = {PAGE_SIZE, PAGE_SIZE * 2};
    iotxn_sg_t sg;
    uint32_t sg_len;
    iotxn_pages_to_sg(paddrs, &sg, sizeof(paddrs) / sizeof(mx_paddr_t), &sg_len);
    ASSERT_EQ(sg_len, 1u, "unexpected sg_len");
    ASSERT_EQ(sg.paddr, paddrs[0], "unexpected address in sg entry");
    ASSERT_EQ(sg.length, (uint64_t)(PAGE_SIZE * 2), "unexpected length in sg entry");
    END_TEST;
}

static bool test_pages_to_sg_aligned(void) {
    BEGIN_TEST;
    mx_paddr_t paddrs[2] = {PAGE_SIZE, PAGE_SIZE * 2};
    iotxn_sg_t sg;
    uint32_t sg_len;
    iotxn_pages_to_sg(paddrs, &sg, sizeof(paddrs) / sizeof(mx_paddr_t), &sg_len);
    ASSERT_EQ(sg_len, 1u, "unexpected sg_len");
    ASSERT_EQ(sg.paddr, paddrs[0], "unexpected address in sg entry");
    ASSERT_EQ(sg.length, (uint64_t)(PAGE_SIZE * 2), "unexpected length in sg entry");
    END_TEST;
}

BEGIN_TEST_CASE(iotxn_tests)
RUN_TEST(test_physmap_simple)
RUN_TEST(test_pages_to_sg_simple)
RUN_TEST(test_pages_to_sg_contiguous)
RUN_TEST(test_pages_to_sg_aligned)
END_TEST_CASE(iotxn_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
