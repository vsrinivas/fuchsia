// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

// zx_object_get_child(ZX_HANDLE_INVALID) should return
// ZX_ERR_BAD_HANDLE. ZX-1702
bool handle_invalid() {
    zx_handle_t process;
    zx_status_t status;
    zx_info_handle_basic_t info;
    zx_handle_t myself = zx_process_self();

    status = zx_object_get_info(myself, ZX_INFO_HANDLE_BASIC,
                                &info, sizeof(info), NULL, NULL);
    ASSERT_EQ(status, ZX_OK);

    status = zx_object_get_child(ZX_HANDLE_INVALID, info.koid,
                                 ZX_RIGHT_SAME_RIGHTS, &process);
    ASSERT_EQ(status, ZX_ERR_BAD_HANDLE);
    return true;
}

BEGIN_TEST_CASE(object_get_child_tests)
RUN_TEST(handle_invalid);
END_TEST_CASE(object_get_child_tests)
