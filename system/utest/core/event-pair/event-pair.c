// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <magenta/syscalls.h>
#include <unittest/unittest.h>

static void check_signals_state(mx_handle_t h, mx_signals_t satisfied, mx_signals_t satisfiable) {
    mx_signals_state_t st = {0};
    EXPECT_EQ(mx_handle_wait_one(h, 0u, 0u, &st), ERR_BAD_STATE, "wrong wait result");
    EXPECT_EQ(st.satisfied, satisfied, "wrong satisfied state");
    EXPECT_EQ(st.satisfiable, satisfiable, "wrong satisfiable state");
}

static bool create_test(void) {
    BEGIN_TEST;

    {
        mx_handle_t h[2] = {MX_HANDLE_INVALID, MX_HANDLE_INVALID};
        ASSERT_EQ(mx_eventpair_create(h, 0u), NO_ERROR, "eventpair_create failed");
        ASSERT_GT(h[0], 0, "invalid handle from eventpair_create");
        ASSERT_GT(h[1], 0, "invalid handle from eventpair_create");

        mx_info_handle_basic_t info;
        memset(&info, 0, sizeof(info));
        ASSERT_EQ(mx_object_get_info(h[0], MX_INFO_HANDLE_BASIC, sizeof(info.rec), &info,
                  sizeof(info)), (mx_ssize_t)sizeof(info), "object_get_info failed");
        EXPECT_EQ(info.rec.rights,
                  MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_WRITE,
                  "wrong rights");
        EXPECT_EQ(info.rec.type, (uint32_t)MX_OBJ_TYPE_EVENT_PAIR, "wrong type");
        memset(&info, 0, sizeof(info));
        ASSERT_EQ(mx_object_get_info(h[1], MX_INFO_HANDLE_BASIC, sizeof(info.rec), &info,
                  sizeof(info)), (mx_ssize_t)sizeof(info), "object_get_info failed");
        EXPECT_EQ(info.rec.rights,
                  MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_WRITE,
                  "wrong rights");
        EXPECT_EQ(info.rec.type, (uint32_t)MX_OBJ_TYPE_EVENT_PAIR, "wrong type");

        EXPECT_EQ(mx_handle_close(h[0]), NO_ERROR, "failed to close event pair handle");
        EXPECT_EQ(mx_handle_close(h[1]), NO_ERROR, "failed to close event pair handle");
    }

    // Currently no flags are supported.
    {
        mx_handle_t h[2] = {MX_HANDLE_INVALID, MX_HANDLE_INVALID};
        EXPECT_EQ(mx_eventpair_create(h, 1u), ERR_NOT_SUPPORTED, "eventpair_create failed to fail");
        EXPECT_EQ(h[0], MX_HANDLE_INVALID, "valid handle from failed eventpair_create?");
        EXPECT_EQ(h[1], MX_HANDLE_INVALID, "valid handle from failed eventpair_create?");
    }

    END_TEST;
}

static bool signal_test(void) {
    BEGIN_TEST;

    mx_handle_t h[2] = {MX_HANDLE_INVALID, MX_HANDLE_INVALID};
    ASSERT_EQ(mx_eventpair_create(h, 0u), NO_ERROR, "eventpair_create failed");
    ASSERT_GT(h[0], 0, "invalid handle from eventpair_create");
    ASSERT_GT(h[1], 0, "invalid handle from eventpair_create");

    check_signals_state(h[0], 0u, MX_SIGNAL_PEER_CLOSED | MX_SIGNAL_SIGNAL_ALL);
    check_signals_state(h[1], 0u, MX_SIGNAL_PEER_CLOSED | MX_SIGNAL_SIGNAL_ALL);

    EXPECT_EQ(mx_object_signal(h[0], 0u, MX_SIGNAL_SIGNAL0), NO_ERROR, "object_signal failed");
    check_signals_state(h[0], 0u, MX_SIGNAL_PEER_CLOSED | MX_SIGNAL_SIGNAL_ALL);
    check_signals_state(h[1], MX_SIGNAL_SIGNAL0, MX_SIGNAL_PEER_CLOSED | MX_SIGNAL_SIGNAL_ALL);

    EXPECT_EQ(mx_object_signal(h[1], 0u, MX_SIGNAL_SIGNAL1 | MX_SIGNAL_SIGNAL2), NO_ERROR,
              "object_signal failed");
    check_signals_state(h[0], MX_SIGNAL_SIGNAL1 | MX_SIGNAL_SIGNAL2,
                        MX_SIGNAL_PEER_CLOSED | MX_SIGNAL_SIGNAL_ALL);
    check_signals_state(h[1], MX_SIGNAL_SIGNAL0, MX_SIGNAL_PEER_CLOSED | MX_SIGNAL_SIGNAL_ALL);

    EXPECT_EQ(mx_object_signal(h[0], MX_SIGNAL_SIGNAL0, MX_SIGNAL_SIGNAL3 | MX_SIGNAL_SIGNAL4),
              NO_ERROR, "object_signal failed");
    check_signals_state(h[0], MX_SIGNAL_SIGNAL1 | MX_SIGNAL_SIGNAL2,
                        MX_SIGNAL_PEER_CLOSED | MX_SIGNAL_SIGNAL_ALL);
    check_signals_state(h[1], MX_SIGNAL_SIGNAL3 | MX_SIGNAL_SIGNAL4,
                        MX_SIGNAL_PEER_CLOSED | MX_SIGNAL_SIGNAL_ALL);

    EXPECT_EQ(mx_handle_close(h[0]), NO_ERROR, "failed to close event pair handle");

    // Signaled flags should remain satisfied but now should now also get peer closed (and
    // unsignaled flags should be unsatisfiable).
    check_signals_state(h[1], MX_SIGNAL_PEER_CLOSED | MX_SIGNAL_SIGNAL3 | MX_SIGNAL_SIGNAL4,
                        MX_SIGNAL_PEER_CLOSED | MX_SIGNAL_SIGNAL3 | MX_SIGNAL_SIGNAL4);

    EXPECT_EQ(mx_handle_close(h[1]), NO_ERROR, "failed to close event pair handle");

    END_TEST;
}

BEGIN_TEST_CASE(event_pair_tests)
RUN_TEST(create_test)
RUN_TEST(signal_test)
END_TEST_CASE(event_pair_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
