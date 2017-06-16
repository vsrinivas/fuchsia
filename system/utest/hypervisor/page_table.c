// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdlib.h>

#include <hypervisor/guest.h>
#include <pretty/hexdump.h>
#include <unittest/unittest.h>

static void hexdump_result(void* actual, void* expected) {
    printf("\nactual:\n");
    hexdump_ex(actual + PAGE_SIZE * 0, 16, PAGE_SIZE * 0);
    hexdump_ex(actual + PAGE_SIZE * 1, 16, PAGE_SIZE * 1);
    hexdump_ex(actual + PAGE_SIZE * 2, 16, PAGE_SIZE * 2);
    hexdump_ex(actual + PAGE_SIZE * 3, 32, PAGE_SIZE * 3);
    printf("expected:\n");
    hexdump_ex(expected + PAGE_SIZE * 0, 16, PAGE_SIZE * 0);
    hexdump_ex(expected + PAGE_SIZE * 1, 16, PAGE_SIZE * 1);
    hexdump_ex(expected + PAGE_SIZE * 2, 16, PAGE_SIZE * 2);
    hexdump_ex(expected + PAGE_SIZE * 3, 32, PAGE_SIZE * 3);
}

#define ASSERT_EPT_EQ(actual, expected, size, msg)            \
    do {                                                      \
      int cmp = memcmp(actual, expected, size);               \
      if (cmp != 0)                                           \
          hexdump_result(actual, expected);                   \
      ASSERT_EQ(cmp, 0, msg);                                 \
    } while (0)

#define INITIALIZE_PAGE_TABLE {{{ 0 }}}

typedef struct {
    uint64_t entries[512];
} page_table;

enum {
    X86_PTE_P    = 0x01,    /* P    Valid           */
    X86_PTE_RW   = 0x02,    /* R/W  Read/Write      */
    X86_PTE_PS   = 0x80,    /* PS   Page size       */
};

static bool page_table_1gb(void) {
    BEGIN_TEST;

    uintptr_t pte_off;
    page_table actual[4] = INITIALIZE_PAGE_TABLE;
    page_table expected[4] = INITIALIZE_PAGE_TABLE;

    ASSERT_EQ(guest_create_page_table((uintptr_t)actual, 1 << 30, &pte_off), MX_OK, "");
    ASSERT_EQ(pte_off, PAGE_SIZE * 2u, "");

    // pml4
    expected[0].entries[0] = PAGE_SIZE | X86_PTE_P | X86_PTE_RW;
    // pdp
    expected[1].entries[0] = X86_PTE_P | X86_PTE_RW | X86_PTE_PS;
    ASSERT_EPT_EQ(actual, expected, sizeof(actual), "");

    END_TEST;
}

static bool page_table_2mb(void) {
    BEGIN_TEST;

    uintptr_t pte_off;
    page_table actual[4] = INITIALIZE_PAGE_TABLE;
    page_table expected[4] = INITIALIZE_PAGE_TABLE;

    ASSERT_EQ(guest_create_page_table((uintptr_t)actual, 2 << 20, &pte_off), MX_OK, "");
    ASSERT_EQ(pte_off, PAGE_SIZE * 3u, "");

    // pml4
    expected[0].entries[0] = PAGE_SIZE | X86_PTE_P | X86_PTE_RW;
    // pdp
    expected[1].entries[0]  = PAGE_SIZE * 2 | X86_PTE_P | X86_PTE_RW;
    // pd
    expected[2].entries[0] = X86_PTE_P | X86_PTE_RW | X86_PTE_PS;
    ASSERT_EPT_EQ(actual, expected, sizeof(actual), "");

    END_TEST;
}

static bool page_table_4kb(void) {
    BEGIN_TEST;

    uintptr_t pte_off;
    page_table actual[4] = INITIALIZE_PAGE_TABLE;
    page_table expected[4] = INITIALIZE_PAGE_TABLE;

    ASSERT_EQ(guest_create_page_table((uintptr_t)actual, 4 * 4 << 10, &pte_off), MX_OK, "");
    ASSERT_EQ(pte_off, PAGE_SIZE * 4u, "");

    // pml4
    expected[0].entries[0] = PAGE_SIZE | X86_PTE_P | X86_PTE_RW;
    // pdp
    expected[1].entries[0]  = PAGE_SIZE * 2 | X86_PTE_P | X86_PTE_RW;
    // pd
    expected[2].entries[0] = PAGE_SIZE * 3 | X86_PTE_P | X86_PTE_RW;
    // pt
    expected[3].entries[0] = PAGE_SIZE * 0 | X86_PTE_P | X86_PTE_RW | X86_PTE_PS;
    expected[3].entries[1] = PAGE_SIZE * 1 | X86_PTE_P | X86_PTE_RW | X86_PTE_PS;
    expected[3].entries[2] = PAGE_SIZE * 2 | X86_PTE_P | X86_PTE_RW | X86_PTE_PS;
    expected[3].entries[3] = PAGE_SIZE * 3 | X86_PTE_P | X86_PTE_RW | X86_PTE_PS;
    ASSERT_EPT_EQ(actual, expected, sizeof(actual), "");

    END_TEST;
}

static bool page_table_mixed_pages(void) {
    BEGIN_TEST;

    uintptr_t pte_off;
    page_table actual[4] = INITIALIZE_PAGE_TABLE;
    page_table expected[4] = INITIALIZE_PAGE_TABLE;

    ASSERT_EQ(guest_create_page_table((uintptr_t)actual, (2 << 20)  + (4 << 10), &pte_off), MX_OK, "");
    ASSERT_EQ(pte_off, PAGE_SIZE * 4u, "");

    // pml4
    expected[0].entries[0] = PAGE_SIZE | X86_PTE_P | X86_PTE_RW;
    // pdp
    expected[1].entries[0]  = PAGE_SIZE * 2 | X86_PTE_P | X86_PTE_RW;
    // pd
    expected[2].entries[0] = X86_PTE_P | X86_PTE_RW | X86_PTE_PS;
    expected[2].entries[1] = PAGE_SIZE * 3 | X86_PTE_P | X86_PTE_RW;
    // pt
    expected[3].entries[0] = (2 << 20) | X86_PTE_P | X86_PTE_RW | X86_PTE_PS;
    ASSERT_EPT_EQ(actual, expected, sizeof(actual), "");

    END_TEST;
}

BEGIN_TEST_CASE(x86_64_extended_page_table)
RUN_TEST(page_table_1gb)
RUN_TEST(page_table_2mb)
RUN_TEST(page_table_4kb)
RUN_TEST(page_table_mixed_pages)
END_TEST_CASE(x86_64_extended_page_table)
