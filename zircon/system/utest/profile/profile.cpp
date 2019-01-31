// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <stdio.h>

#include <lib/fdio/io.h>
#include <lib/fdio/util.h>

#include <unittest/unittest.h>

#include <zircon/syscalls.h>
#include <fuchsia/scheduler/c/fidl.h>

static bool get_profile(void) {
    BEGIN_TEST;

    zx_handle_t ch[2];
    ASSERT_EQ(ZX_OK, zx_channel_create(0u, &ch[0], &ch[1]), "channel create");

    ASSERT_EQ(ZX_OK, fdio_service_connect(
        "/svc/" fuchsia_scheduler_ProfileProvider_Name, ch[0]), "connect");

    zx_handle_t profile = ZX_HANDLE_INVALID;
    zx_status_t status = ZX_ERR_NOT_SUPPORTED;
    EXPECT_EQ(ZX_OK, fuchsia_scheduler_ProfileProviderGetProfile(
        ch[1], 0u, "<test>", 6, &status, &profile), "");

    EXPECT_EQ(ZX_OK, status, "profile create");

    zx_info_handle_basic_t info;
    ASSERT_EQ(
        zx_object_get_info(profile, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr),
        ZX_OK, "object_get_info");
    EXPECT_NE(info.koid, 0, "no koid");
    EXPECT_EQ(info.type, ZX_OBJ_TYPE_PROFILE, "incorrect type");

    zx_handle_close(profile);
    zx_handle_close(ch[0]);
    zx_handle_close(ch[1]);

    END_TEST;
}


BEGIN_TEST_CASE(sched_profiles)
RUN_TEST(get_profile);
END_TEST_CASE(sched_profiles)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
