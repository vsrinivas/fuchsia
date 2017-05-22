// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdlib.h>

#include <hypervisor/guest.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>
#include <pretty/hexdump.h>
#include <unittest/unittest.h>

static const uint64_t kVmoSize = 2 << 20;

extern const char guest_start[];
extern const char guest_end[];

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

#if __x86_64__
enum {
    X86_PTE_P    = 0x01,    /* P    Valid           */
    X86_PTE_RW   = 0x02,    /* R/W  Read/Write      */
    X86_PTE_PS   = 0x80,    /* PS   Page size       */
};

static bool guest_create_page_table_x86_64_test(void) {
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
#endif // __x86_64__

static bool guest_start_test(void) {
    BEGIN_TEST;

    mx_handle_t hypervisor;
    mx_status_t status = mx_hypervisor_create(MX_HANDLE_INVALID, 0, &hypervisor);
    // The hypervisor isn't supported, so don't run the test.
    if (status == ERR_NOT_SUPPORTED)
        return true;
    ASSERT_EQ(status, NO_ERROR, "");

    uintptr_t addr;
    mx_handle_t guest_phys_mem;
    ASSERT_EQ(guest_create_phys_mem(&addr, kVmoSize, &guest_phys_mem), NO_ERROR, "");

    mx_handle_t guest_ctl_fifo;
    mx_handle_t guest;
    ASSERT_EQ(guest_create(hypervisor, guest_phys_mem, &guest_ctl_fifo, &guest), NO_ERROR, "");

    // Setup the guest.
    uintptr_t guest_ip = 0;
    ASSERT_EQ(guest_create_page_table(addr, kVmoSize, &guest_ip), NO_ERROR, "");

#if __x86_64__
    memcpy((void*)(addr + guest_ip), guest_start, guest_end - guest_start);
    uintptr_t guest_cr3 = 0;
    ASSERT_EQ(mx_hypervisor_op(guest, MX_HYPERVISOR_OP_GUEST_SET_ENTRY_CR3,
                               &guest_cr3, sizeof(guest_cr3), NULL, 0),
              NO_ERROR, "");
#endif // __x86_64__

    // Enter the guest.
    ASSERT_EQ(mx_hypervisor_op(guest, MX_HYPERVISOR_OP_GUEST_SET_ENTRY_IP,
                               &guest_ip, sizeof(guest_ip), NULL, 0),
              NO_ERROR, "");
    ASSERT_EQ(mx_hypervisor_op(guest, MX_HYPERVISOR_OP_GUEST_ENTER, NULL, 0, NULL, 0),
              ERR_STOP, "");

    mx_guest_packet_t packet[2];
    uint32_t num_packets;
    ASSERT_EQ(mx_fifo_read(guest_ctl_fifo, packet, sizeof(packet), &num_packets), NO_ERROR, "");
    ASSERT_EQ(num_packets, 2u, "");
    ASSERT_EQ(packet[0].type, MX_GUEST_PKT_TYPE_IO_PORT, "");
    ASSERT_EQ(packet[0].io_port.access_size, 1u, "");
    ASSERT_EQ(packet[0].io_port.data[0], 'm', "");
    ASSERT_EQ(packet[1].type, MX_GUEST_PKT_TYPE_IO_PORT, "");
    ASSERT_EQ(packet[1].io_port.access_size, 1u, "");
    ASSERT_EQ(packet[1].io_port.data[0], 'x', "");

    ASSERT_EQ(mx_handle_close(guest), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(guest_ctl_fifo), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(guest_phys_mem), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(hypervisor), NO_ERROR, "");

    END_TEST;
}

BEGIN_TEST_CASE(hypervisors)
#if __x86_64__
RUN_TEST(guest_create_page_table_x86_64_test)
#endif // __x86_64__
RUN_TEST(guest_start_test)
END_TEST_CASE(hypervisors)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
