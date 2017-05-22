// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>

#include <hypervisor/guest.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>
#include <unittest/unittest.h>

static const uint64_t kVmoSize = 2 << 20;

extern const char guest_start[];
extern const char guest_end[];

static bool guest_enter(void) {
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

    mx_handle_t guest_apic_mem;
    ASSERT_EQ(mx_vmo_create(PAGE_SIZE, 0, &guest_apic_mem), NO_ERROR, "");
    ASSERT_EQ(mx_hypervisor_op(guest, MX_HYPERVISOR_OP_GUEST_SET_APIC_MEM,
                               &guest_apic_mem, sizeof(guest_apic_mem), NULL, 0),
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

#if __x86_64__
    ASSERT_EQ(mx_handle_close(guest_apic_mem), NO_ERROR, "");
#endif // __x86_64__

    END_TEST;
}

BEGIN_TEST_CASE(guest)
RUN_TEST(guest_enter)
END_TEST_CASE(guest)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
