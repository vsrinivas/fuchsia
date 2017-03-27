// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <limits.h>

#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>
#include <unittest/unittest.h>

static const uint32_t kAllocateFlags = MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE;
static const uint32_t kMapFlags = MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE;

static bool guest_start_test(void) {
    BEGIN_TEST;

    mx_handle_t hypervisor;
    mx_status_t status = mx_hypervisor_create(MX_HANDLE_INVALID, 0, &hypervisor);
    // The hypervisor isn't supported, so don't run the test.
    if (status == ERR_NOT_SUPPORTED)
        return true;
    ASSERT_EQ(status, NO_ERROR, "");

    mx_handle_t vmo;
    status = mx_vmo_create(2 << 20, 0, &vmo);

    mx_handle_t guest;
    ASSERT_EQ(mx_hypervisor_op(hypervisor, MX_HYPERVISOR_OP_GUEST_CREATE,
                               &vmo, sizeof(vmo), &guest, sizeof(guest)),
              NO_ERROR, "");

    // Setup guest page tables.
    mx_handle_t vmar;
    uintptr_t addr;
    ASSERT_EQ(mx_vmar_allocate(mx_vmar_root_self(), 0, PAGE_SIZE, kAllocateFlags, &vmar, &addr),
              NO_ERROR, "");
    uintptr_t mapped_addr;
    ASSERT_EQ(mx_vmar_map(vmar, 0, vmo, 0, PAGE_SIZE, kMapFlags, &mapped_addr), NO_ERROR, "");

#if __x86_64__
    memset((void*)mapped_addr, 0, PAGE_SIZE);
    uintptr_t guest_cr3 = 0;
    ASSERT_EQ(mx_hypervisor_op(guest, MX_HYPERVISOR_OP_GUEST_SET_CR3,
                               &guest_cr3, sizeof(guest_cr3), NULL, 0),
              NO_ERROR, "");
#endif // __x86_64__

    ASSERT_EQ(mx_hypervisor_op(guest, MX_HYPERVISOR_OP_GUEST_START, NULL, 0, NULL, 0),
              NO_ERROR, "");

    ASSERT_EQ(mx_handle_close(vmar), NO_ERROR, "");
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
