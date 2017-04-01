// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>

#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>
#include <unittest/unittest.h>

static const uint32_t kMapFlags = MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE;
static const uint64_t kVmoSize = 2 << 20;

extern const char guest_start[];
extern const char guest_end[];

#if __x86_64__
#define X86_MMU_PG_P    0x0001  /* P    Valid           */
#define X86_MMU_PG_RW   0x0002  /* R/W  Read/Write      */
#define X86_MMU_PG_U    0x0004  /* U/S  User/Supervisor */
#define X86_MMU_PG_PS   0x0080  /* PS   Page size       */

static uintptr_t guest_setup(uint8_t* addr) {
    memset(addr, 0, kVmoSize);

    uint64_t* pml4 = (uint64_t*)addr;
    uint64_t* pdp = (uint64_t*)(addr + PAGE_SIZE);
    uint64_t* pd = (uint64_t*)(addr + PAGE_SIZE * 2);
    uint64_t* ip = (uint64_t*)(addr + PAGE_SIZE * 3);

    *pml4 = PAGE_SIZE | X86_MMU_PG_P | X86_MMU_PG_RW | X86_MMU_PG_U;
    *pdp = PAGE_SIZE * 2 | X86_MMU_PG_P | X86_MMU_PG_RW | X86_MMU_PG_U;
    *pd = X86_MMU_PG_P | X86_MMU_PG_RW | X86_MMU_PG_U | X86_MMU_PG_PS;

    memcpy(ip, guest_start, guest_end - guest_start);
    return PAGE_SIZE * 3;
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

    mx_handle_t vmo;
    status = mx_vmo_create(kVmoSize, 0, &vmo);

    mx_handle_t guest;
    ASSERT_EQ(mx_hypervisor_op(hypervisor, MX_HYPERVISOR_OP_GUEST_CREATE,
                               &vmo, sizeof(vmo), &guest, sizeof(guest)),
              NO_ERROR, "");

    // Setup the guest.
    uintptr_t mapped_addr;
    ASSERT_EQ(mx_vmar_map(mx_vmar_root_self(), 0, vmo, 0, kVmoSize, kMapFlags, &mapped_addr),
              NO_ERROR, "");
    uintptr_t guest_entry = 0;
#if __x86_64__
    guest_entry = guest_setup((uint8_t*)mapped_addr);
    uintptr_t guest_cr3 = 0;
    ASSERT_EQ(mx_hypervisor_op(guest, MX_HYPERVISOR_OP_GUEST_SET_CR3,
                               &guest_cr3, sizeof(guest_cr3), NULL, 0),
              NO_ERROR, "");
#endif // __x86_64__

    ASSERT_EQ(mx_hypervisor_op(guest, MX_HYPERVISOR_OP_GUEST_SET_ENTRY,
                               &guest_entry, sizeof(guest_entry), NULL, 0),
              NO_ERROR, "");

    ASSERT_EQ(mx_hypervisor_op(guest, MX_HYPERVISOR_OP_GUEST_ENTER, NULL, 0, NULL, 0),
              NO_ERROR, "");
    ASSERT_EQ(mx_hypervisor_op(guest, MX_HYPERVISOR_OP_GUEST_ENTER, NULL, 0, NULL, 0),
              NO_ERROR, "");

    ASSERT_EQ(mx_handle_close(guest), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(vmo), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(hypervisor), NO_ERROR, "");

    END_TEST;
}

BEGIN_TEST_CASE(hypervisors)
RUN_TEST(guest_start_test)
END_TEST_CASE(hypervisors)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
