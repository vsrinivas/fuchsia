// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include <lib/zx/event.h>
#include <lib/zx/job.h>

// Tests in this file rely that the default job is the root job.

static bool retrieve_lowmem_test() {
    BEGIN_TEST;

    zx::event lowmem;

    ASSERT_EQ(zx_system_get_event(ZX_HANDLE_INVALID, ZX_SYSTEM_EVENT_LOW_MEMORY,
                                  lowmem.reset_and_get_address()),
              ZX_ERR_BAD_HANDLE, "cannot get with invalid root job");

    ASSERT_EQ(zx_system_get_event(zx_process_self(), ZX_SYSTEM_EVENT_LOW_MEMORY,
                                  lowmem.reset_and_get_address()),
              ZX_ERR_WRONG_TYPE, "cannot get without a job handle");

    zx::job tmp_job;
    ASSERT_EQ(zx::job::create(*zx::job::default_job(), 0u, &tmp_job), ZX_OK, "helper sub job");

    ASSERT_EQ(zx_system_get_event(tmp_job.get(), ZX_SYSTEM_EVENT_LOW_MEMORY,
                                  lowmem.reset_and_get_address()),
              ZX_ERR_ACCESS_DENIED, "cannot get without correct root job");

    zx::unowned_job root_job(zx_job_default());
    if (!root_job->is_valid()) {
        unittest_printf("no root job. skipping part of test\n");
    } else {
        ASSERT_EQ(zx_system_get_event(root_job->get(), ~0u, lowmem.reset_and_get_address()),
                  ZX_ERR_INVALID_ARGS, "incorrect kind value does not retrieve");

        ASSERT_EQ(zx_system_get_event(root_job->get(), ZX_SYSTEM_EVENT_LOW_MEMORY,
                                      lowmem.reset_and_get_address()),
                  ZX_OK, "can get if root provided");

        // Confirm we at least got an event.
        zx_info_handle_basic_t info;
        ASSERT_EQ(zx_object_get_info(lowmem.get(), ZX_INFO_HANDLE_BASIC, &info, sizeof(info),
                                     nullptr, nullptr),
                  ZX_OK, "object_get_info");
        EXPECT_NE(info.koid, 0, "no koid");
        EXPECT_EQ(info.type, ZX_OBJ_TYPE_EVENT, "incorrect type");
        EXPECT_EQ(info.rights, ZX_DEFAULT_SYSTEM_EVENT_LOW_MEMORY_RIGHTS, "incorrect rights");
    }

    END_TEST;
}

static bool cannot_signal_lowmem_from_userspace_test(void) {
    BEGIN_TEST;

    zx::unowned_job root_job(zx_job_default());
    if (!root_job->is_valid()) {
        unittest_printf("no root job. skipping test\n");
    } else {
        zx::event lowmem;
        ASSERT_EQ(zx_system_get_event(root_job->get(), ZX_SYSTEM_EVENT_LOW_MEMORY,
                                      lowmem.reset_and_get_address()),
                  ZX_OK);

        ASSERT_EQ(lowmem.signal(0, 1), ZX_ERR_ACCESS_DENIED, "shouldn't be able to signal");
    }

    END_TEST;
}

BEGIN_TEST_CASE(system_event_tests)
RUN_TEST(retrieve_lowmem_test)
RUN_TEST(cannot_signal_lowmem_from_userspace_test)
END_TEST_CASE(system_event_tests)
