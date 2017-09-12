// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <unittest/unittest.h>

static void check_signals_state(zx_handle_t h, zx_signals_t satisfied) {
    zx_signals_t pending = 0;
    EXPECT_EQ(zx_object_wait_one(h, 0u, 0u, &pending), ZX_ERR_TIMED_OUT, "wrong wait result");
    EXPECT_EQ(pending, satisfied, "wrong satisfied state");
}

static bool create_test(void) {
    BEGIN_TEST;

    {
        zx_handle_t h[2] = {ZX_HANDLE_INVALID, ZX_HANDLE_INVALID};
        ASSERT_EQ(zx_eventpair_create(0, &h[0], &h[1]), ZX_OK, "eventpair_create failed");
        ASSERT_NE(h[0], ZX_HANDLE_INVALID, "invalid handle from eventpair_create");
        ASSERT_NE(h[1], ZX_HANDLE_INVALID, "invalid handle from eventpair_create");

        zx_info_handle_basic_t info;
        memset(&info, 0, sizeof(info));
        zx_status_t status = zx_object_get_info(h[0], ZX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL);
        ASSERT_EQ(status, ZX_OK, "");
        EXPECT_EQ(info.rights,
                  ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ |
                  ZX_RIGHT_WRITE | ZX_RIGHT_SIGNAL | ZX_RIGHT_SIGNAL_PEER,
                  "wrong rights");
        EXPECT_EQ(info.type, (uint32_t)ZX_OBJ_TYPE_EVENT_PAIR, "wrong type");
        memset(&info, 0, sizeof(info));
        status = zx_object_get_info(h[1], ZX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL);
        ASSERT_EQ(status, ZX_OK, "");
        EXPECT_EQ(info.rights,
                  ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ |
                  ZX_RIGHT_WRITE | ZX_RIGHT_SIGNAL | ZX_RIGHT_SIGNAL_PEER,
                  "wrong rights");
        EXPECT_EQ(info.type, (uint32_t)ZX_OBJ_TYPE_EVENT_PAIR, "wrong type");

        EXPECT_EQ(zx_handle_close(h[0]), ZX_OK, "failed to close event pair handle");
        EXPECT_EQ(zx_handle_close(h[1]), ZX_OK, "failed to close event pair handle");
    }

    // Currently no flags are supported.
    {
        zx_handle_t h[2] = {ZX_HANDLE_INVALID, ZX_HANDLE_INVALID};
        EXPECT_EQ(zx_eventpair_create(1u, &h[0], &h[1]), ZX_ERR_NOT_SUPPORTED, "eventpair_create failed to fail");
        EXPECT_EQ(h[0], ZX_HANDLE_INVALID, "valid handle from failed eventpair_create?");
        EXPECT_EQ(h[1], ZX_HANDLE_INVALID, "valid handle from failed eventpair_create?");
    }

    END_TEST;
}

static bool signal_test(void) {
    BEGIN_TEST;
    zx_handle_t h[2] = {ZX_HANDLE_INVALID, ZX_HANDLE_INVALID};
    ASSERT_EQ(zx_eventpair_create(0, &h[0], &h[1]), ZX_OK, "eventpair_create failed");
    ASSERT_NE(h[0], ZX_HANDLE_INVALID, "invalid handle from eventpair_create");
    ASSERT_NE(h[1], ZX_HANDLE_INVALID, "invalid handle from eventpair_create");

    check_signals_state(h[0], ZX_SIGNAL_LAST_HANDLE);
    check_signals_state(h[1], ZX_SIGNAL_LAST_HANDLE);

    EXPECT_EQ(zx_object_signal(h[0], 0u, ZX_USER_SIGNAL_0), ZX_OK, "object_signal failed");
    check_signals_state(h[1], ZX_SIGNAL_LAST_HANDLE);
    check_signals_state(h[0], ZX_USER_SIGNAL_0 | ZX_SIGNAL_LAST_HANDLE);

    EXPECT_EQ(zx_object_signal(h[0], ZX_USER_SIGNAL_0, 0u), ZX_OK, "object_signal failed");
    check_signals_state(h[1], ZX_SIGNAL_LAST_HANDLE);
    check_signals_state(h[0], ZX_SIGNAL_LAST_HANDLE);

    EXPECT_EQ(zx_handle_close(h[0]), ZX_OK, "failed to close event pair handle");
    check_signals_state(h[1], ZX_EPAIR_PEER_CLOSED | ZX_SIGNAL_LAST_HANDLE);
    EXPECT_EQ(zx_handle_close(h[1]), ZX_OK, "failed to close event pair handle");
    END_TEST;
}

static bool signal_peer_test(void) {
    BEGIN_TEST;

    zx_handle_t h[2] = {ZX_HANDLE_INVALID, ZX_HANDLE_INVALID};
    ASSERT_EQ(zx_eventpair_create(0, &h[0], &h[1]), ZX_OK, "eventpair_create failed");
    ASSERT_NE(h[0], ZX_HANDLE_INVALID, "invalid handle from eventpair_create");
    ASSERT_NE(h[1], ZX_HANDLE_INVALID, "invalid handle from eventpair_create");

    EXPECT_EQ(zx_object_signal_peer(h[0], 0u, ZX_USER_SIGNAL_0), ZX_OK, "object_signal failed");
    check_signals_state(h[0], ZX_SIGNAL_LAST_HANDLE);
    check_signals_state(h[1], ZX_USER_SIGNAL_0 | ZX_SIGNAL_LAST_HANDLE);

    EXPECT_EQ(zx_object_signal_peer(h[1], 0u, ZX_USER_SIGNAL_1 | ZX_USER_SIGNAL_2), ZX_OK,
              "object_signal failed");
    check_signals_state(h[0], ZX_USER_SIGNAL_1 | ZX_USER_SIGNAL_2 | ZX_SIGNAL_LAST_HANDLE);
    check_signals_state(h[1], ZX_USER_SIGNAL_0 | ZX_SIGNAL_LAST_HANDLE);

    EXPECT_EQ(zx_object_signal_peer(h[0], ZX_USER_SIGNAL_0, ZX_USER_SIGNAL_3 | ZX_USER_SIGNAL_4),
              ZX_OK, "object_signal failed");
    check_signals_state(h[0], ZX_USER_SIGNAL_1 | ZX_USER_SIGNAL_2 | ZX_SIGNAL_LAST_HANDLE);
    check_signals_state(h[1], ZX_USER_SIGNAL_3 | ZX_USER_SIGNAL_4 | ZX_SIGNAL_LAST_HANDLE);

    EXPECT_EQ(zx_handle_close(h[0]), ZX_OK, "failed to close event pair handle");

    // Signaled flags should remain satisfied but now should now also get peer closed (and
    // unsignaled flags should be unsatisfiable).
    check_signals_state(h[1],
        ZX_EPAIR_PEER_CLOSED | ZX_USER_SIGNAL_3 | ZX_USER_SIGNAL_4 | ZX_SIGNAL_LAST_HANDLE);

    EXPECT_EQ(zx_handle_close(h[1]), ZX_OK, "failed to close event pair handle");

    END_TEST;
}

BEGIN_TEST_CASE(event_pair_tests)
RUN_TEST(create_test)
RUN_TEST(signal_test)
RUN_TEST(signal_peer_test)
END_TEST_CASE(event_pair_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
