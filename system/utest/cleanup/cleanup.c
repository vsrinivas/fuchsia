// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <threads.h>
#include <unistd.h>

#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <unittest/unittest.h>

static const char* msg = "This is a test message, please discard.";

static volatile int test_state = 0;

int watchdog(void* arg) {
    usleep(1000 * 1000);
    EXPECT_GE(test_state, 100, "cleanup-test: FAILED. Stuck waiting in test");
    return 0;
}

bool cleanup_test(void) {
    BEGIN_TEST;
    zx_handle_t p0[2], p1[2];
    zx_signals_t pending;
    zx_status_t r;

    thrd_t thread;
    thrd_create_with_name(&thread, watchdog, NULL, "watchdog");
    thrd_detach(thread);

    // TEST1
    // Create a channel, close one end, try to wait on the other.
    test_state = 1;
    r = zx_channel_create(0, p1, p1 + 1);
    ASSERT_EQ(r, 0, "cleanup-test: channel create 1 failed");

    zx_handle_close(p1[1]);
    unittest_printf("cleanup-test: about to wait, should return immediately with PEER_CLOSED\n");
    r = zx_object_wait_one(p1[0], ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                 ZX_TIME_INFINITE, &pending);
    ASSERT_EQ(r, 0, "cleanup-test: FAILED");

    ASSERT_EQ(pending, ZX_CHANNEL_PEER_CLOSED, "cleanup-test: FAILED");
    unittest_printf("cleanup-test: SUCCESS, observed PEER_CLOSED signal\n\n");
    zx_handle_close(p1[0]);

    // TEST2
    // Create a channel, close one end. Then create an event and write a
    // message on the channel sending the event along. The event normally
    // dissapears from this process handle table but since the message_write
    // fails (because the other end is closed) The event should still
    // be usable from this process.
    test_state = 2;
    r = zx_channel_create(0, p1, p1 + 1);
    ASSERT_EQ(r, 0, "cleanup-test: channel create 1 failed");
    zx_handle_close(p1[1]);

    zx_handle_t event = ZX_HANDLE_INVALID;
    r = zx_event_create(0u, &event);
    ASSERT_EQ(r, 0, "");
    ASSERT_NE(event, ZX_HANDLE_INVALID, "cleanup-test: event create failed");
    r = zx_channel_write(p1[0], 0, &msg, sizeof(msg), &event, 1);
    ASSERT_EQ(r, ZX_ERR_PEER_CLOSED, "cleanup-test: unexpected message_write return code");

    r = zx_object_signal(event, 0u, ZX_EVENT_SIGNALED);
    ASSERT_GE(r, 0, "cleanup-test: unable to signal event!");
    unittest_printf("cleanup-test: SUCCESS, event is alive\n\n");

    zx_handle_close(event);
    zx_handle_close(p1[0]);

    // TEST3
    // Simulates the case where we prepare a message channel with a
    // message+channelhandle already in it and the far end closed,
    // like we pass to newly created processes, but then (say
    // process creation fails), we delete the other end of the
    // channel we were going to send.  At this point we expect
    // that the channel handle bundled with the message should
    // be closed and waiting on the opposing handle should
    // signal PEER_CLOSED.
    test_state = 3;
    r = zx_channel_create(0, p0, p0 + 1);
    ASSERT_EQ(r, 0, "cleanup-test: channel create 0 failed");

    r = zx_channel_create(0, p1, p1 + 1);
    ASSERT_EQ(r, 0, "cleanup-test: channel create 1 failed");

    r = zx_channel_write(p0[0], 0, &msg, sizeof(msg), &p1[1], 1);
    ASSERT_GE(r, 0, "cleanup-test: channel write failed");

    zx_handle_close(p0[0]);
    zx_handle_close(p0[1]);

    unittest_printf("cleanup-test: about to wait, should return immediately with PEER_CLOSED\n");
    r = zx_object_wait_one(p1[0], ZX_CHANNEL_PEER_CLOSED, ZX_TIME_INFINITE, NULL);
    ASSERT_EQ(r, 0, "cleanup-test: FAILED");

    test_state = 100;
    unittest_printf("cleanup-test: PASSED\n");
    zx_handle_close(p1[0]);
    END_TEST;
}

BEGIN_TEST_CASE(cleanup_tests)
RUN_TEST(cleanup_test)
END_TEST_CASE(cleanup_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
