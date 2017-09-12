// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdio.h>
#include <threads.h>

#include <zx/channel.h>
#include <zx/event.h>
#include <zx/handle.h>
#include <zx/port.h>

#include <fbl/type_support.h>

#include <zircon/syscalls/port.h>

#include <unistd.h>
#include <unittest/unittest.h>

static bool basic_test() {
    // Test that"
    // 1- handles starts with the ZX_SIGNAL_LAST_HANDLE active.
    // 2- the signal deactives on duplication.
    // 3- the signal comes back on closing the duplicated handle.
    // 4- the ZX_SIGNAL_LAST_HANDLE cannot be touched with zx_object_signal().

    BEGIN_TEST;
    zx::event event;
    ASSERT_EQ(zx::event::create(0u, &event), ZX_OK);
    zx_signals_t observed = 0u;
    EXPECT_EQ(
        event.wait_one(ZX_SIGNAL_LAST_HANDLE, ZX_TIME_INFINITE, &observed), ZX_OK);
    EXPECT_EQ(observed, ZX_SIGNAL_LAST_HANDLE);

    zx::event dup;
    EXPECT_EQ(event.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup), ZX_OK);

    EXPECT_EQ(event.wait_one(ZX_SIGNAL_LAST_HANDLE, 0u, &observed), ZX_ERR_TIMED_OUT);
    EXPECT_EQ(observed, 0u);

    dup.reset();
    EXPECT_EQ(
        event.wait_one(ZX_SIGNAL_LAST_HANDLE, ZX_TIME_INFINITE, &observed), ZX_OK);
    EXPECT_EQ(observed, ZX_SIGNAL_LAST_HANDLE);

    EXPECT_EQ(event.signal(ZX_SIGNAL_LAST_HANDLE, 0), ZX_ERR_INVALID_ARGS);
    END_TEST;
}

static bool replace_test() {
    // Test that:
    // 1- replacing the handle keeps the ZX_SIGNAL_LAST_HANDLE signal.
    // 2- replacing a duplicate does not spuriosly signal ZX_SIGNAL_LAST_HANDLE.
    // 3- closing the replacement does signal ZX_SIGNAL_LAST_HANDLE.
    // Note: we rely on a port to detect the edge transition, if any.

    BEGIN_TEST;
    zx::event old_ev;
    ASSERT_EQ(zx::event::create(0u, &old_ev), ZX_OK);

    zx::event new_ev;
    EXPECT_EQ(old_ev.replace(ZX_RIGHT_SAME_RIGHTS, &new_ev), ZX_OK);

    zx_signals_t observed = 0u;
    EXPECT_EQ(
        new_ev.wait_one(ZX_SIGNAL_LAST_HANDLE, ZX_TIME_INFINITE, &observed), ZX_OK);
    EXPECT_EQ(observed, ZX_SIGNAL_LAST_HANDLE);

    zx::event dup;
    EXPECT_EQ(new_ev.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup), ZX_OK);

    zx::port port;
    ASSERT_EQ(zx::port::create(0, &port), ZX_OK);

    EXPECT_EQ(new_ev.wait_async(
        port, 1u, ZX_SIGNAL_LAST_HANDLE, ZX_WAIT_ASYNC_ONCE), ZX_OK);

    zx_port_packet_t packet = {};
    EXPECT_EQ(port.wait(0ull, &packet, 0u), ZX_ERR_TIMED_OUT);

    zx::event new_dup;
    EXPECT_EQ(dup.replace(ZX_RIGHT_SAME_RIGHTS, &new_dup), ZX_OK);
    EXPECT_EQ(port.wait(0ull, &packet, 0u), ZX_ERR_TIMED_OUT);

    new_dup.reset();
    EXPECT_EQ(port.wait(ZX_TIME_INFINITE, &packet, 0u), ZX_OK);
    EXPECT_EQ(packet.type, ZX_PKT_TYPE_SIGNAL_ONE);
    EXPECT_EQ(packet.signal.observed, ZX_SIGNAL_LAST_HANDLE);

    END_TEST;
}

static bool channel_test() {
    // Test that:
    // 1- Sending/receiving a duplicated object never triggers ZX_SIGNAL_LAST_HANDLE. The
    //    handle count is still 2, even though one handle is not accessible to
    //    any process.
    // 2- Sending an object and closing the send side of a channel does not trigger
    //    ZX_SIGNAL_LAST_HANDLE.
    // 3- Closing the receive side of #2 does trigger ZX_SIGNAL_LAST_HANDLE.

    BEGIN_TEST;
    zx::event event;
    ASSERT_EQ(zx::event::create(0u, &event), ZX_OK);

    zx::channel channel[2];
    ASSERT_EQ(zx::channel::create(0u, &channel[0], &channel[1]), ZX_OK);

    zx::port port;
    ASSERT_EQ(zx::port::create(0, &port), ZX_OK);

    zx_handle_t dup_ev;
    EXPECT_EQ(zx_handle_duplicate(event.get(), ZX_RIGHT_SAME_RIGHTS, &dup_ev), ZX_OK);

    ASSERT_EQ(event.wait_async(
        port, 1u, ZX_SIGNAL_LAST_HANDLE, ZX_WAIT_ASYNC_ONCE), ZX_OK);

    uint32_t actual_b;
    uint32_t actual_h;
    zx_port_packet_t packet = {};

    for (int ix = 0; ix != 4; ++ix) {
        ASSERT_EQ(channel[0].write(0u, nullptr, 0u, &dup_ev, 1u), ZX_OK);
        dup_ev = 0u;

        EXPECT_EQ(port.wait(0ull, &packet, 0u), ZX_ERR_TIMED_OUT);

        ASSERT_EQ(channel[1].read(
            0u, nullptr, 0, &actual_b, &dup_ev, 1u, &actual_h), ZX_OK);

        EXPECT_EQ(port.wait(0ull, &packet, 0u), ZX_ERR_TIMED_OUT);
    }

    ASSERT_EQ(channel[0].write(0u, nullptr, 0u, &dup_ev, 1u), ZX_OK);

    channel[0].reset();
    EXPECT_EQ(port.wait(0ull, &packet, 0u), ZX_ERR_TIMED_OUT);

    channel[1].reset();
    EXPECT_EQ(port.wait(ZX_TIME_INFINITE, &packet, 0u), ZX_OK);
    EXPECT_EQ(packet.type, ZX_PKT_TYPE_SIGNAL_ONE);
    EXPECT_EQ(packet.signal.observed, ZX_SIGNAL_LAST_HANDLE);

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
