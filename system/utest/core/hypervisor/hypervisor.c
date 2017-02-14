// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/syscalls.h>
#include <unittest/unittest.h>

static bool hypervisor_create_test(void) {
    BEGIN_TEST;

    mx_handle_t handle;
    ASSERT_EQ(mx_hypervisor_create(MX_HANDLE_INVALID, 0, &handle), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(handle), NO_ERROR, "");

    END_TEST;
}

BEGIN_TEST_CASE(hypervisors)
RUN_TEST(hypervisor_create_test)
END_TEST_CASE(hypervisors)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
