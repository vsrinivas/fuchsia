// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <unittest/unittest.h>
#include <zircon/sanitizer.h>
#include <zircon/syscalls.h>

bool publish_data(void) {
    BEGIN_TEST;

    zx_handle_t handle;
    zx_status_t status = zx_vmo_create(1, 0, &handle);
    EXPECT_EQ(status, ZX_OK, "failed to create VMO");
    zx_object_set_property(handle, ZX_PROP_NAME, "test", 5);
    __sanitizer_publish_data("test", handle);

    END_TEST;
}

BEGIN_TEST_CASE(publish_data_helper_tests)
RUN_TEST(publish_data)
END_TEST_CASE(publish_data_helper_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
