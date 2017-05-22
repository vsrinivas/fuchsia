// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdlib.h>

#include <hypervisor/guest.h>
#include <pretty/hexdump.h>
#include <unittest/unittest.h>

static void hexdump_result(uint8_t* actual, uint8_t* expected) {
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

enum {
    X86_PTE_P    = 0x01,    /* P    Valid           */
    X86_PTE_RW   = 0x02,    /* R/W  Read/Write      */
    X86_PTE_PS   = 0x80,    /* PS   Page size       */
};

static bool page_table_x86_64(void) {
    BEGIN_TEST;

    size_t size = PAGE_SIZE * 4;
    uintptr_t pte_off;
    void* actual = malloc(size);
    void* expected = malloc(size);
    memset(expected, 0, size);

    // Test 1GB page.
    memset(actual, 0, size);
    ASSERT_EQ(guest_create_page_table((uintptr_t)actual, 1 << 30, &pte_off), NO_ERROR, "");
    ASSERT_EQ(pte_off, PAGE_SIZE * 2u, "");

    memset(expected, 0, size);
    uint64_t* pml4 = (uint64_t*)expected;
    uint64_t* pdp = (uint64_t*)(expected + PAGE_SIZE);
    *pml4 = PAGE_SIZE | X86_PTE_P | X86_PTE_RW;
    *pdp = X86_PTE_P | X86_PTE_RW | X86_PTE_PS;

    int cmp = memcmp(actual, expected, size);
    if (cmp != 0)
        hexdump_result(actual, expected);
    ASSERT_EQ(cmp, 0, "");

    // Test 2MB page.
    memset(actual, 0, size);
    ASSERT_EQ(guest_create_page_table((uintptr_t)actual, 2 << 20, &pte_off), NO_ERROR, "");
    ASSERT_EQ(pte_off, PAGE_SIZE * 3u, "");

    memset(expected, 0, size);
    uint64_t* pd = (uint64_t*)(expected + PAGE_SIZE * 2);
    *pml4 = PAGE_SIZE | X86_PTE_P | X86_PTE_RW;
    *pdp = PAGE_SIZE * 2 | X86_PTE_P | X86_PTE_RW;
    *pd = X86_PTE_P | X86_PTE_RW | X86_PTE_PS;

    cmp = memcmp(actual, expected, size);
    if (cmp != 0)
        hexdump_result(actual, expected);
    ASSERT_EQ(cmp, 0, "");

    // Test 4KB page.
    memset(actual, 0, size);
    ASSERT_EQ(guest_create_page_table((uintptr_t)actual, 4 * 4 << 10, &pte_off), NO_ERROR, "");
    ASSERT_EQ(pte_off, PAGE_SIZE * 4u, "");

    memset(expected, 0, size);
    uint64_t* pt = (uint64_t*)(expected + PAGE_SIZE * 3);
    *pml4 = PAGE_SIZE | X86_PTE_P | X86_PTE_RW;
    *pdp = PAGE_SIZE * 2 | X86_PTE_P | X86_PTE_RW;
    *pd = PAGE_SIZE * 3 | X86_PTE_P | X86_PTE_RW;
    pt[0] = PAGE_SIZE * 0 | X86_PTE_P | X86_PTE_RW | X86_PTE_PS;
    pt[1] = PAGE_SIZE * 1 | X86_PTE_P | X86_PTE_RW | X86_PTE_PS;
    pt[2] = PAGE_SIZE * 2 | X86_PTE_P | X86_PTE_RW | X86_PTE_PS;
    pt[3] = PAGE_SIZE * 3 | X86_PTE_P | X86_PTE_RW | X86_PTE_PS;

    cmp = memcmp(actual, expected, size);
    if (cmp != 0)
        hexdump_result(actual, expected);
    ASSERT_EQ(cmp, 0, "");

    // Test mixed pages.
    memset(actual, 0, size);
    ASSERT_EQ(guest_create_page_table((uintptr_t)actual, (2 << 20)  + (4 << 10), &pte_off), NO_ERROR, "");
    ASSERT_EQ(pte_off, PAGE_SIZE * 4u, "");

    memset(expected, 0, size);
    *pml4 = PAGE_SIZE | X86_PTE_P | X86_PTE_RW;
    *pdp = PAGE_SIZE * 2 | X86_PTE_P | X86_PTE_RW;
    pd[0] = X86_PTE_P | X86_PTE_RW | X86_PTE_PS;
    pd[1] = PAGE_SIZE * 3 | X86_PTE_P | X86_PTE_RW;
    *pt = (2 << 20) | X86_PTE_P | X86_PTE_RW | X86_PTE_PS;

    cmp = memcmp(actual, expected, size);
    if (cmp != 0)
        hexdump_result(actual, expected);
    ASSERT_EQ(cmp, 0, "");

    END_TEST;
}

BEGIN_TEST_CASE(page_table)
RUN_TEST(page_table_x86_64)
END_TEST_CASE(page_table)
