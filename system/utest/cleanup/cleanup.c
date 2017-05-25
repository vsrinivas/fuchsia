// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <threads.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <unittest/unittest.h>

static const char* msg = "This is a test message, please discard.";

volatile int test_state = 0;

int watchdog(void* arg) {
    usleep(1000 * 1000);
    EXPECT_GE(test_state, 100, "cleanup-test: FAILED. Stuck waiting in test");
    return 0;
}

bool cleanup_test(void) {
    BEGIN_TEST;
    mx_handle_t p0[2], p1[2];
    mx_signals_t pending;
    mx_status_t r;

    thrd_t thread;
    thrd_create_with_name(&thread, watchdog, NULL, "watchdog");
    thrd_detach(thread);

    // TEST1
    // Create a channel, close one end, try to wait on the other.
    test_state = 1;
    r = mx_channel_create(0, p1, p1 + 1);
    ASSERT_EQ(r, 0, "cleanup-test: channel create 1 failed");

    mx_handle_close(p1[1]);
    unittest_printf("cleanup-test: about to wait, should return immediately with PEER_CLOSED\n");
    r = mx_object_wait_one(p1[0], MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED,
                                 MX_TIME_INFINITE, &pending);
    ASSERT_EQ(r, 0, "cleanup-test: FAILED");

    ASSERT_EQ(pending, MX_CHANNEL_PEER_CLOSED | MX_SIGNAL_LAST_HANDLE, "cleanup-test: FAILED");
    unittest_printf("cleanup-test: SUCCESS, observed PEER_CLOSED signal\n\n");
    mx_handle_close(p1[0]);

    // TEST2
    // Create a channel, close one end. Then create an event and write a
    // message on the channel sending the event along. The event normally
    // dissapears from this process handle table but since the message_write
    // fails (because the other end is closed) The event should still
    // be usable from this process.
    test_state = 2;
    r = mx_channel_create(0, p1, p1 + 1);
    ASSERT_EQ(r, 0, "cleanup-test: channel create 1 failed");
    mx_handle_close(p1[1]);

    mx_handle_t event;
    r = mx_event_create(0u, &event);
    ASSERT_EQ(r, 0, "");
    ASSERT_GE(event, 0, "cleanup-test: event create failed");
    r = mx_channel_write(p1[0], 0, &msg, sizeof(msg), &event, 1);
    ASSERT_EQ(r, ERR_PEER_CLOSED, "cleanup-test: unexpected message_write return code");

    r = mx_object_signal(event, 0u, MX_EVENT_SIGNALED);
    ASSERT_GE(r, 0, "cleanup-test: unable to signal event!");
    unittest_printf("cleanup-test: SUCCESS, event is alive\n\n");

    mx_handle_close(event);
    mx_handle_close(p1[0]);

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
    r = mx_channel_create(0, p0, p0 + 1);
    ASSERT_EQ(r, 0, "cleanup-test: channel create 0 failed");

    r = mx_channel_create(0, p1, p1 + 1);
    ASSERT_EQ(r, 0, "cleanup-test: channel create 1 failed");

    r = mx_channel_write(p0[0], 0, &msg, sizeof(msg), &p1[1], 1);
    ASSERT_GE(r, 0, "cleanup-test: channel write failed");

    mx_handle_close(p0[0]);
    mx_handle_close(p0[1]);

    unittest_printf("cleanup-test: about to wait, should return immediately with PEER_CLOSED\n");
    r = mx_object_wait_one(p1[0], MX_CHANNEL_PEER_CLOSED, MX_TIME_INFINITE, NULL);
    ASSERT_EQ(r, 0, "cleanup-test: FAILED");

    test_state = 100;
    unittest_printf("cleanup-test: PASSED\n");
    mx_handle_close(p1[0]);
    END_TEST;
}

BEGIN_TEST_CASE(cleanup_tests)
RUN_TEST(cleanup_test)
END_TEST_CASE(cleanup_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
