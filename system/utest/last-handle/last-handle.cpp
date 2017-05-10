// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdio.h>
#include <threads.h>

#include <mx/channel.h>
#include <mx/event.h>
#include <mx/handle.h>
#include <mx/port.h>

#include <mxtl/type_support.h>

#include <magenta/syscalls/port.h>

#include <unistd.h>
#include <unittest/unittest.h>

static bool basic_test() {
    // Test that"
    // 1- handles starts with the MX_SIGNAL_LAST_HANDLE active.
    // 2- the signal deactives on duplication.
    // 3- the signal comes back on closing the duplicated handle.
    // 4- the MX_SIGNAL_LAST_HANDLE cannot be touched with mx_object_signal().

    BEGIN_TEST;
    mx::event event;
    ASSERT_EQ(mx::event::create(0u, &event), NO_ERROR, "");
    mx_signals_t observed = 0u;
    EXPECT_EQ(
        event.wait_one(MX_SIGNAL_LAST_HANDLE, MX_TIME_INFINITE, &observed), NO_ERROR, "");
    EXPECT_EQ(observed, MX_SIGNAL_LAST_HANDLE, "");

    mx::event dup;
    EXPECT_EQ(event.duplicate(MX_RIGHT_SAME_RIGHTS, &dup), NO_ERROR, "");

    EXPECT_EQ(event.wait_one(MX_SIGNAL_LAST_HANDLE, 0u, &observed), ERR_TIMED_OUT, "");
    EXPECT_EQ(observed, 0u, "");

    dup.reset();
    EXPECT_EQ(
        event.wait_one(MX_SIGNAL_LAST_HANDLE, MX_TIME_INFINITE, &observed), NO_ERROR, "");
    EXPECT_EQ(observed, MX_SIGNAL_LAST_HANDLE, "");

    EXPECT_EQ(event.signal(MX_SIGNAL_LAST_HANDLE, 0), ERR_INVALID_ARGS, "");
    END_TEST;
}

static bool replace_test() {
    // Test that:
    // 1- replacing the handle keeps the MX_SIGNAL_LAST_HANDLE signal.
    // 2- replacing a duplicate does not spuriosly signal MX_SIGNAL_LAST_HANDLE.
    // 3- closing the replacement does signal MX_SIGNAL_LAST_HANDLE.
    // Note: we rely on a port to detect the edge transition, if any.

    BEGIN_TEST;
    mx::event old_ev;
    ASSERT_EQ(mx::event::create(0u, &old_ev), NO_ERROR, "");

    mx::event new_ev;
    EXPECT_EQ(old_ev.replace(MX_RIGHT_SAME_RIGHTS, &new_ev), NO_ERROR, "");

    mx_signals_t observed = 0u;
    EXPECT_EQ(
        new_ev.wait_one(MX_SIGNAL_LAST_HANDLE, MX_TIME_INFINITE, &observed), NO_ERROR, "");
    EXPECT_EQ(observed, MX_SIGNAL_LAST_HANDLE, "");

    mx::event dup;
    EXPECT_EQ(new_ev.duplicate(MX_RIGHT_SAME_RIGHTS, &dup), NO_ERROR, "");

    mx::port port;
    ASSERT_EQ(mx::port::create(MX_PORT_OPT_V2, &port), NO_ERROR, "");

    EXPECT_EQ(new_ev.wait_async(
        port, 1u, MX_SIGNAL_LAST_HANDLE, MX_WAIT_ASYNC_ONCE), NO_ERROR, "");

    mx_port_packet_t packet = {};
    EXPECT_EQ(port.wait(0ull, &packet, 0u), ERR_TIMED_OUT, "");

    mx::event new_dup;
    EXPECT_EQ(dup.replace(MX_RIGHT_SAME_RIGHTS, &new_dup), NO_ERROR, "");
    EXPECT_EQ(port.wait(0ull, &packet, 0u), ERR_TIMED_OUT, "");

    new_dup.reset();
    EXPECT_EQ(port.wait(MX_TIME_INFINITE, &packet, 0u), NO_ERROR, "");
    EXPECT_EQ(packet.type, MX_PKT_TYPE_SIGNAL_ONE, "");
    EXPECT_EQ(packet.signal.observed, MX_SIGNAL_LAST_HANDLE, "");

    END_TEST;
}

static bool channel_test() {
    // Test that:
    // 1- Sending/receiving a duplicated object never triggers MX_SIGNAL_LAST_HANDLE. The
    //    handle count is still 2, even though one handle is not accessible to
    //    any process.
    // 2- Sending an object and closing the send side of a channel does not trigger
    //    MX_SIGNAL_LAST_HANDLE.
    // 3- Closing the receive side of #2 does trigger MX_SIGNAL_LAST_HANDLE.

    BEGIN_TEST;
    mx::event event;
    ASSERT_EQ(mx::event::create(0u, &event), NO_ERROR, "");

    mx::channel channel[2];
    ASSERT_EQ(mx::channel::create(0u, &channel[0], &channel[1]), NO_ERROR, "");

    mx::port port;
    ASSERT_EQ(mx::port::create(MX_PORT_OPT_V2, &port), NO_ERROR, "");

    mx_handle_t dup_ev;
    EXPECT_EQ(mx_handle_duplicate(event.get(), MX_RIGHT_SAME_RIGHTS, &dup_ev), NO_ERROR, "");

    ASSERT_EQ(event.wait_async(
        port, 1u, MX_SIGNAL_LAST_HANDLE, MX_WAIT_ASYNC_ONCE), NO_ERROR, "");

    uint32_t actual_b;
    uint32_t actual_h;
    mx_port_packet_t packet = {};

    for (int ix = 0; ix != 4; ++ix) {
        ASSERT_EQ(channel[0].write(0u, nullptr, 0u, &dup_ev, 1u), NO_ERROR, "");
        dup_ev = 0u;

        EXPECT_EQ(port.wait(0ull, &packet, 0u), ERR_TIMED_OUT, "");

        ASSERT_EQ(channel[1].read(
            0u, nullptr, 0, &actual_b, &dup_ev, 1u, &actual_h), NO_ERROR, "");

        EXPECT_EQ(port.wait(0ull, &packet, 0u), ERR_TIMED_OUT, "");
    }

    ASSERT_EQ(channel[0].write(0u, nullptr, 0u, &dup_ev, 1u), NO_ERROR, "");

    channel[0].reset();
    EXPECT_EQ(port.wait(0ull, &packet, 0u), ERR_TIMED_OUT, "");

    channel[1].reset();
    EXPECT_EQ(port.wait(MX_TIME_INFINITE, &packet, 0u), NO_ERROR, "");
    EXPECT_EQ(packet.type, MX_PKT_TYPE_SIGNAL_ONE, "");
    EXPECT_EQ(packet.signal.observed, MX_SIGNAL_LAST_HANDLE, "");

    END_TEST;
}

BEGIN_TEST_CASE(last_handle)
RUN_TEST(basic_test)
RUN_TEST(replace_test)
RUN_TEST(channel_test)
END_TEST_CASE(last_handle)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
