// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>

#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <unittest/unittest.h>

static bool handle_info_test(void) {
    BEGIN_TEST;

    mx_handle_t event;
    ASSERT_EQ(mx_event_create(0u, &event), 0, "");
    mx_handle_t duped;
    mx_status_t status = mx_handle_duplicate(event, MX_RIGHT_SAME_RIGHTS, &duped);
    ASSERT_EQ(status, MX_OK, "");

    ASSERT_EQ(mx_object_get_info(event, MX_INFO_HANDLE_VALID, NULL, 0u, NULL, NULL), MX_OK,
              "handle should be valid");
    ASSERT_EQ(mx_handle_close(event), MX_OK, "failed to close the handle");
    ASSERT_EQ(mx_object_get_info(event, MX_INFO_HANDLE_VALID, NULL, 0u, NULL, NULL), MX_ERR_BAD_HANDLE,
              "handle should be valid");

    mx_info_handle_basic_t info = {};
    ASSERT_EQ(mx_object_get_info(duped, MX_INFO_HANDLE_BASIC, &info, 4u, NULL, NULL),
              MX_ERR_BUFFER_TOO_SMALL, "bad struct size validation");

    status = mx_object_get_info(duped, MX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL);
    ASSERT_EQ(status, MX_OK, "handle should be valid");

    const mx_rights_t evr = MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER |
                            MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_SIGNAL;

    EXPECT_GT(info.koid, 0ULL, "object id should be positive");
    EXPECT_EQ(info.type, (uint32_t)MX_OBJ_TYPE_EVENT, "handle should be an event");
    EXPECT_EQ(info.rights, evr, "wrong set of rights");
    EXPECT_EQ(info.props, (uint32_t)MX_OBJ_PROP_WAITABLE, "");
    EXPECT_EQ(info.related_koid, 0ULL, "events don't have associated koid");

    mx_handle_close(event);
    mx_handle_close(duped);

    END_TEST;
}

static bool handle_related_koid_test(void) {
    BEGIN_TEST;

    mx_info_handle_basic_t info0 = {};
    mx_info_handle_basic_t info1 = {};

    mx_status_t status = mx_object_get_info(
        mx_job_default(), MX_INFO_HANDLE_BASIC, &info0, sizeof(info0), NULL, NULL);
    ASSERT_EQ(status, MX_OK, "");

    status = mx_object_get_info(
        mx_process_self(), MX_INFO_HANDLE_BASIC, &info1, sizeof(info1), NULL, NULL);
    ASSERT_EQ(status, MX_OK, "");

    EXPECT_EQ(info0.type, (uint32_t)MX_OBJ_TYPE_JOB, "");
    EXPECT_EQ(info1.type, (uint32_t)MX_OBJ_TYPE_PROCESS, "");

    mx_handle_t thread;
    status = mx_thread_create(mx_process_self(), "hitr", 4, 0u, &thread);
    ASSERT_EQ(status, MX_OK, "");

    mx_info_handle_basic_t info2 = {};

    status = mx_object_get_info(
        thread, MX_INFO_HANDLE_BASIC, &info2, sizeof(info2), NULL, NULL);
    ASSERT_EQ(status, MX_OK, "");

    EXPECT_EQ(info2.type, (uint32_t)MX_OBJ_TYPE_THREAD, "");

    // The related koid of a process is its job and this test assumes that the
    // default job is in fact the parent job of this test. Equivalently, a thread's
    // associated koid is the process koid.
    EXPECT_EQ(info1.related_koid, info0.koid, "");
    EXPECT_EQ(info2.related_koid, info1.koid, "");

    mx_handle_close(thread);

    mx_handle_t sock0, sock1;
    status = mx_socket_create(0u, &sock0, &sock1);
    ASSERT_EQ(status, MX_OK, "");

    status = mx_object_get_info(
        sock0, MX_INFO_HANDLE_BASIC, &info0, sizeof(info0), NULL, NULL);
    ASSERT_EQ(status, MX_OK, "");

    status = mx_object_get_info(
        sock1, MX_INFO_HANDLE_BASIC, &info1, sizeof(info1), NULL, NULL);
    ASSERT_EQ(status, MX_OK, "");

    EXPECT_EQ(info0.type, (uint32_t)MX_OBJ_TYPE_SOCKET, "");
    EXPECT_EQ(info1.type, (uint32_t)MX_OBJ_TYPE_SOCKET, "");

    // The related koid of a socket pair are each other koids.
    EXPECT_EQ(info0.related_koid, info1.koid, "");
    EXPECT_EQ(info1.related_koid, info0.koid, "");

    mx_handle_close(sock0);
    mx_handle_close(sock1);
    END_TEST;
}

static bool handle_rights_test(void) {
    BEGIN_TEST;

    mx_handle_t event;
    ASSERT_EQ(mx_event_create(0u, &event), 0, "");
    mx_handle_t duped_ro;
    mx_status_t status = mx_handle_duplicate(event, MX_RIGHT_READ, &duped_ro);
    ASSERT_EQ(status, MX_OK, "");

    mx_info_handle_basic_t info = {};
    status = mx_object_get_info(duped_ro, MX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL);
    ASSERT_EQ(status, MX_OK, "handle should be valid");

    ASSERT_EQ(info.rights, MX_RIGHT_READ, "wrong set of rights");

    mx_handle_t h;
    status = mx_handle_duplicate(duped_ro, MX_RIGHT_SAME_RIGHTS, &h);
    ASSERT_EQ(status, MX_ERR_ACCESS_DENIED, "should fail rights check");

    status = mx_handle_duplicate(event, MX_RIGHT_EXECUTE | MX_RIGHT_READ, &h);
    ASSERT_EQ(status, MX_ERR_INVALID_ARGS, "cannot upgrade rights");

    ASSERT_EQ(mx_handle_replace(duped_ro, MX_RIGHT_EXECUTE | MX_RIGHT_READ, &h), MX_ERR_INVALID_ARGS,
              "cannot upgrade rights");

    status = mx_handle_replace(duped_ro, MX_RIGHT_SAME_RIGHTS, &h);
    ASSERT_EQ(status, MX_OK, "should be able to replace handle");
    // duped_ro should now be invalid.

    ASSERT_EQ(mx_handle_close(event), MX_OK, "failed to close original handle");
    ASSERT_EQ(mx_handle_close(duped_ro), MX_ERR_BAD_HANDLE, "replaced handle should be invalid");
    ASSERT_EQ(mx_handle_close(h), MX_OK, "failed to close replacement handle");

    END_TEST;
}

BEGIN_TEST_CASE(handle_info_tests)
RUN_TEST(handle_info_test)
RUN_TEST(handle_related_koid_test)
RUN_TEST(handle_rights_test)
END_TEST_CASE(handle_info_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
