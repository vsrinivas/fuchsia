// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>
#include <unittest/unittest.h>

static bool guest_start_test(void) {
    BEGIN_TEST;

    mx_handle_t hypervisor;
    mx_status_t status = mx_hypervisor_create(MX_HANDLE_INVALID, 0, &hypervisor);
    // The hypervisor isn't supported, so don't run the test.
    if (status == ERR_NOT_SUPPORTED)
        return true;
    ASSERT_EQ(status, NO_ERROR, "");

    mx_handle_t vmo;
    status = mx_vmo_create(1 << 20, 0, &vmo);

    mx_handle_t guest;
    ASSERT_EQ(mx_hypervisor_op(hypervisor, MX_HYPERVISOR_OP_GUEST_CREATE,
                               &vmo, sizeof(vmo), &guest, sizeof(guest)),
              NO_ERROR, "");

    ASSERT_EQ(mx_hypervisor_op(guest, MX_HYPERVISOR_OP_GUEST_START,
                               NULL, 0, NULL, 0),
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
