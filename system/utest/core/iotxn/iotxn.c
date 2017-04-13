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
    ASSERT_EQ(iotxn_alloc(&txn, 0, PAGE_SIZE * 3), NO_ERROR, "");
    ASSERT_NONNULL(txn, "");
    ASSERT_EQ(iotxn_physmap(txn), NO_ERROR, "");
    ASSERT_NONNULL(txn->phys, "expected phys to be set");
    ASSERT_EQ(txn->phys_count, 3u, "unexpected phys_count");
    iotxn_release(txn);
    END_TEST;
}

static bool test_physmap_clone(void) {
    BEGIN_TEST;
    iotxn_t* txn;
    ASSERT_EQ(iotxn_alloc(&txn, 0, PAGE_SIZE * 3), NO_ERROR, "");
    ASSERT_NONNULL(txn, "");
    ASSERT_EQ(iotxn_physmap(txn), NO_ERROR, "");
    ASSERT_NONNULL(txn->phys, "expected phys to be set");
    ASSERT_EQ(txn->phys_count, 3u, "unexpected phys_count");

    iotxn_t* clone;
    ASSERT_EQ(iotxn_clone(txn, &clone), NO_ERROR, "");
    ASSERT_EQ(txn->phys, clone->phys, "expected clone to point to the same phys");
    ASSERT_EQ(txn->phys_count, clone->phys_count, "unexpected clone phys_count");
    iotxn_release(txn);
    iotxn_release(clone);
    END_TEST;
}

static bool test_physmap_aligned_offset(void) {
    BEGIN_TEST;
    iotxn_t* txn;
    ASSERT_EQ(iotxn_alloc(&txn, 0, PAGE_SIZE * 3), NO_ERROR, "");
    ASSERT_NONNULL(txn, "");
    txn->vmo_offset = PAGE_SIZE;
    txn->vmo_length = PAGE_SIZE * 2;
    ASSERT_EQ(iotxn_physmap(txn), NO_ERROR, "");
    ASSERT_NONNULL(txn->phys, "expected phys to be set");
    ASSERT_EQ(txn->phys_count, 2u, "unexpected phys_count");
    iotxn_release(txn);
    END_TEST;
}

static bool test_physmap_unaligned_offset(void) {
    BEGIN_TEST;
    iotxn_t* txn;
    ASSERT_EQ(iotxn_alloc(&txn, 0, PAGE_SIZE * 3), NO_ERROR, "");
    ASSERT_NONNULL(txn, "");
    txn->vmo_offset = PAGE_SIZE / 2;
    txn->vmo_length = PAGE_SIZE * 2;
    ASSERT_EQ(iotxn_physmap(txn), NO_ERROR, "");
    ASSERT_NONNULL(txn->phys, "expected phys to be set");
    ASSERT_EQ(txn->phys_count, 3u, "unexpected phys_count");
    iotxn_release(txn);
    END_TEST;
}

static bool test_physmap_unaligned_offset2(void) {
    BEGIN_TEST;
    iotxn_t* txn;
    ASSERT_EQ(iotxn_alloc(&txn, 0, PAGE_SIZE * 4), NO_ERROR, "");
    ASSERT_NONNULL(txn, "");
    txn->vmo_offset = PAGE_SIZE - (PAGE_SIZE / 4);
    txn->vmo_length = (PAGE_SIZE * 2) + (PAGE_SIZE / 2);
    ASSERT_EQ(iotxn_physmap(txn), NO_ERROR, "");
    ASSERT_NONNULL(txn->phys, "expected phys to be set");
    ASSERT_EQ(txn->phys_count, 4u, "unexpected phys_count");
    iotxn_release(txn);
    END_TEST;
}

BEGIN_TEST_CASE(iotxn_tests)
RUN_TEST(test_physmap_simple)
RUN_TEST(test_physmap_clone)
RUN_TEST(test_physmap_aligned_offset)
RUN_TEST(test_physmap_unaligned_offset)
RUN_TEST(test_physmap_unaligned_offset2)
END_TEST_CASE(iotxn_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
