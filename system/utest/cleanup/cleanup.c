// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <unittest/unittest.h>

static const char* msg = "This is a test message, please discard.";

volatile int test_state = 0;

int watchdog(void* arg) {
    _magenta_nanosleep(1000 * 1000 * 1000);
    EXPECT_GE(test_state, 100, "cleanup-test: FAILED. Stuck waiting in test");
    _magenta_thread_exit();
    return 0;
}

bool cleanup_test(void) {
    BEGIN_TEST;
    mx_handle_t p0tx, p0rx, p1tx, p1rx;
    mx_signals_t pending;
    mx_status_t r;

    _magenta_thread_create(watchdog, NULL, "watchdog", 8);

    // TEST1
    // Create a pipe, close one end, try to wait on the other.
    test_state = 1;
    p1tx = _magenta_message_pipe_create(&p1rx);
    ASSERT_GE(p1tx, 0, "cleanup-test: pipe create 1 failed");

    _magenta_handle_close(p1rx);
    unittest_printf("cleanup-test: about to wait, should return immediately with PEER_CLOSED\n");
    r = _magenta_handle_wait_one(p1tx, MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
                                 MX_TIME_INFINITE, &pending, NULL);
    ASSERT_EQ(r, 0, "cleanup-test: FAILED");

    ASSERT_EQ(pending, MX_SIGNAL_PEER_CLOSED, "cleanup-test: FAILED");
    unittest_printf("cleanup-test: SUCCESS, observed PEER_CLOSED signal\n\n");
    _magenta_handle_close(p1tx);
    _magenta_handle_close(p1rx);

    // TEST2
    // Create a pipe, close one end. Then create an event and write a
    // message on the pipe sending the event along. The event normally
    // dissapears from this process handle table but since the message_write
    // fails (because the other end is closed) The event should still
    // be usable from this process.
    test_state = 2;
    p1tx = _magenta_message_pipe_create(&p1rx);
    ASSERT_GE(p1tx, 0, "cleanup-test: pipe create 1 failed");
    _magenta_handle_close(p1rx);

    mx_handle_t event = _magenta_event_create(0u);

    ASSERT_GE(event, 0, "cleanup-test: event create failed");
    r = _magenta_message_write(p1tx, &msg, sizeof(msg), &event, 1, 0);
    ASSERT_EQ(r, ERR_BAD_STATE, "cleanup-test: unexpected message_write return code");

    r = _magenta_event_signal(event);
    ASSERT_GE(r, 0, "cleanup-test: unable to signal event!");
    unittest_printf("cleanup-test: SUCCESS, event is alive\n\n");

    _magenta_handle_close(event);
    _magenta_handle_close(p1tx);

    // TEST3
    // Simulates the case where we prepare a message pipe with a
    // message+pipehandle already in it and the far end closed,
    // like we pass to newly created processes, but then (say
    // process creation fails), we delete the other end of the
    // pipe we were going to send.  At this point we expect
    // that the pipe handle bundled with the message should
    // be closed and waiting on the opposing handle should
    // signal PEER_CLOSED.
    test_state = 3;
    p0tx = _magenta_message_pipe_create(&p0rx);
    ASSERT_GE(p1tx, 0, "cleanup-test: pipe create 0 failed");

    p1tx = _magenta_message_pipe_create(&p1rx);
    ASSERT_GE(p1tx, 0, "cleanup-test: pipe create 1 failed");

    r = _magenta_message_write(p0tx, &msg, sizeof(msg), &p1rx, 1, 0);
    ASSERT_GE(r, 0, "cleanup-test: pipe write failed");

    _magenta_handle_close(p0tx);
    _magenta_handle_close(p0rx);

    unittest_printf("cleanup-test: about to wait, should return immediately with PEER_CLOSED\n");
    r = _magenta_handle_wait_one(p1tx, MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
                                 MX_TIME_INFINITE, &pending, NULL);
    ASSERT_EQ(r, 0, "cleanup-test: FAILED");

    ASSERT_EQ(pending, MX_SIGNAL_PEER_CLOSED, "cleanup-test: FAILED");

    test_state = 100;
    unittest_printf("cleanup-test: PASSED\n");
    _magenta_handle_close(p1tx);
    END_TEST;
}

BEGIN_TEST_CASE(cleanup_tests)
RUN_TEST(cleanup_test)
END_TEST_CASE(cleanup_tests)

int main(void) {
    return unittest_run_all_tests() ? 0 : -1;
}
