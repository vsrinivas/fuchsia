// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdlib.h>

#include <hypervisor/guest.h>
#include <pretty/hexdump.h>
#include <unittest/unittest.h>

static void* page_addr(void* base, size_t page) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(base);
    addr += PAGE_SIZE * page;
    return reinterpret_cast<void*>(addr);
}

static void hexdump_result(void* actual, void* expected) {
    printf("\nactual:\n");
    hexdump_ex(page_addr(actual, 0), 16, PAGE_SIZE * 0);
    hexdump_ex(page_addr(actual, 1), 16, PAGE_SIZE * 1);
    hexdump_ex(page_addr(actual, 2), 16, PAGE_SIZE * 2);
    hexdump_ex(page_addr(actual, 3), 32, PAGE_SIZE * 3);
    printf("expected:\n");
    hexdump_ex(page_addr(expected, 0), 16, PAGE_SIZE * 0);
    hexdump_ex(page_addr(expected, 1), 16, PAGE_SIZE * 1);
    hexdump_ex(page_addr(expected, 2), 16, PAGE_SIZE * 2);
    hexdump_ex(page_addr(expected, 3), 32, PAGE_SIZE * 3);
}

#define ASSERT_EPT_EQ(actual, expected, size, ...)                                                 \
    do {                                                                                           \
        int cmp = memcmp(actual, expected, size);                                                  \
        if (cmp != 0)                                                                              \
            hexdump_result(actual, expected);                                                      \
        ASSERT_EQ(cmp, 0, ##__VA_ARGS__);                                                          \
    } while (0)

#define INITIALIZE_PAGE_TABLE                                                                      \
    {                                                                                              \
        {                                                                                          \
            { 0 }                                                                                  \
        }                                                                                          \
    }

typedef struct {
    uint64_t entries[512];
} page_table;

enum {
    X86_PTE_P = 0x01,  /* P    Valid           */
    X86_PTE_RW = 0x02, /* R/W  Read/Write      */
    X86_PTE_PS = 0x80, /* PS   Page size       */
};

static bool page_table_1gb(void) {
    BEGIN_TEST;

    uintptr_t pte_off;
    page_table actual[4] = INITIALIZE_PAGE_TABLE;
    page_table expected[4] = INITIALIZE_PAGE_TABLE;

    ASSERT_EQ(guest_create_page_table((uintptr_t)actual, 1 << 30, &pte_off), MX_OK);

    // pml4
    expected[0].entries[0] = PAGE_SIZE | X86_PTE_P | X86_PTE_RW;
    // pdp
    expected[1].entries[0] = X86_PTE_P | X86_PTE_RW | X86_PTE_PS;
    ASSERT_EPT_EQ(actual, expected, sizeof(actual));
    ASSERT_EQ(pte_off, PAGE_SIZE * 2u);

    END_TEST;
}

static bool page_table_2mb(void) {
    BEGIN_TEST;

    uintptr_t pte_off;
    page_table actual[4] = INITIALIZE_PAGE_TABLE;
    page_table expected[4] = INITIALIZE_PAGE_TABLE;

    ASSERT_EQ(guest_create_page_table((uintptr_t)actual, 2 << 20, &pte_off), MX_OK);

    // pml4
    expected[0].entries[0] = PAGE_SIZE | X86_PTE_P | X86_PTE_RW;
    // pdp
    expected[1].entries[0] = PAGE_SIZE * 2 | X86_PTE_P | X86_PTE_RW;
    // pd
    expected[2].entries[0] = X86_PTE_P | X86_PTE_RW | X86_PTE_PS;
    ASSERT_EPT_EQ(actual, expected, sizeof(actual));
    ASSERT_EQ(pte_off, PAGE_SIZE * 3u);

    END_TEST;
}

static bool page_table_4kb(void) {
    BEGIN_TEST;

    uintptr_t pte_off;
    page_table actual[4] = INITIALIZE_PAGE_TABLE;
    page_table expected[4] = INITIALIZE_PAGE_TABLE;

    ASSERT_EQ(guest_create_page_table((uintptr_t)actual, 4 * 4 << 10, &pte_off), MX_OK);

    // pml4
    expected[0].entries[0] = PAGE_SIZE | X86_PTE_P | X86_PTE_RW;
    // pdp
    expected[1].entries[0] = PAGE_SIZE * 2 | X86_PTE_P | X86_PTE_RW;
    // pd
    expected[2].entries[0] = PAGE_SIZE * 3 | X86_PTE_P | X86_PTE_RW;
    // pt
    expected[3].entries[0] = PAGE_SIZE * 0 | X86_PTE_P | X86_PTE_RW;
    expected[3].entries[1] = PAGE_SIZE * 1 | X86_PTE_P | X86_PTE_RW;
    expected[3].entries[2] = PAGE_SIZE * 2 | X86_PTE_P | X86_PTE_RW;
    expected[3].entries[3] = PAGE_SIZE * 3 | X86_PTE_P | X86_PTE_RW;
    ASSERT_EPT_EQ(actual, expected, sizeof(actual));
    ASSERT_EQ(pte_off, PAGE_SIZE * 4u);

    END_TEST;
}

static bool page_table_mixed_pages(void) {
    BEGIN_TEST;

    uintptr_t pte_off;
    page_table actual[4] = INITIALIZE_PAGE_TABLE;
    page_table expected[4] = INITIALIZE_PAGE_TABLE;

    ASSERT_EQ(guest_create_page_table((uintptr_t)actual, (2 << 20) + (4 << 10), &pte_off), MX_OK);

    // pml4
    expected[0].entries[0] = PAGE_SIZE | X86_PTE_P | X86_PTE_RW;
    // pdp
    expected[1].entries[0] = PAGE_SIZE * 2 | X86_PTE_P | X86_PTE_RW;

    // pd
    expected[2].entries[0] = X86_PTE_P | X86_PTE_RW | X86_PTE_PS;
    expected[2].entries[1] = PAGE_SIZE * 3 | X86_PTE_P | X86_PTE_RW;

    // pt
    expected[3].entries[0] = (2 << 20) | X86_PTE_P | X86_PTE_RW;
    ASSERT_EPT_EQ(actual, expected, sizeof(actual));
    ASSERT_EQ(pte_off, PAGE_SIZE * 4u);

    END_TEST;
}

// Create a page table for 2gb + 123mb + 32kb bytes.
static bool page_table_complex(void) {
    BEGIN_TEST;

    uintptr_t pte_off;
    page_table actual[4] = INITIALIZE_PAGE_TABLE;
    page_table expected[4] = INITIALIZE_PAGE_TABLE;

    // 2gb + 123mb + 32kb of RAM. This breaks down as follows:
    //
    // PML4
    // > 1 pointer to a PDPT
    //
    // PDPT
    // > 2 direct-mapped 1gb regions
    // > 1 ponter to a PD
    //
    // PD
    // > 61 direct-mapped 2mb regions
    // > 1 pointer to a PT
    //
    // PT
    // >  264 mapped pages
    ASSERT_EQ(guest_create_page_table((uintptr_t)actual, 0x87B08000, &pte_off), MX_OK);

    // pml4
    expected[0].entries[0] = PAGE_SIZE | X86_PTE_P | X86_PTE_RW;

    // pdp
    expected[1].entries[0] = (0l << 30) | X86_PTE_P | X86_PTE_RW | X86_PTE_PS;
    expected[1].entries[1] = (1l << 30) | X86_PTE_P | X86_PTE_RW | X86_PTE_PS;
    expected[1].entries[2] = PAGE_SIZE * 2 | X86_PTE_P | X86_PTE_RW;

    // pd - starts at 2GB
    const uint64_t pdp_offset = 2l << 30;
    for (int i = 0; i < 62; ++i) {
        expected[2].entries[i] = (pdp_offset + (i << 21)) | X86_PTE_P | X86_PTE_RW | X86_PTE_PS;
    }
    expected[2].entries[61] = PAGE_SIZE * 3 | X86_PTE_P | X86_PTE_RW;

    // pt - starts at 2GB + 122MB
    const uint64_t pd_offset = pdp_offset + (61l << 21);
    for (int i = 0; i < 264; ++i) {
        expected[3].entries[i] = (pd_offset + (i << 12)) | X86_PTE_P | X86_PTE_RW;
    }
    ASSERT_EPT_EQ(actual, expected, sizeof(actual));
    ASSERT_EQ(pte_off, PAGE_SIZE * 4u);

    END_TEST;
}

BEGIN_TEST_CASE(extended_page_table)
RUN_TEST(page_table_1gb)
RUN_TEST(page_table_2mb)
RUN_TEST(page_table_4kb)
RUN_TEST(page_table_mixed_pages)
RUN_TEST(page_table_complex)
END_TEST_CASE(extended_page_table)
