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

static const char* msg = "This is a test message, please discard.";

volatile int test_state = 0;

int watchdog(void* arg) {
    _magenta_nanosleep(100 * 1000 * 1000);
    if (test_state < 100) {
        printf("cleanup-test: FAILED. Stuck waiting in test%d\n", test_state);
    }
    _magenta_thread_exit();
    return 0;
}

int main(int argc, char** argv) {
    mx_handle_t p0tx, p0rx, p1tx, p1rx;
    mx_signals_t pending;
    mx_status_t r;

    _magenta_thread_create(watchdog, NULL, "watchdog", 8);

    // TEST1
    // Create a pipe, close one end, try to wait on the other.
    test_state = 1;
    if ((p1tx = _magenta_message_pipe_create(&p1rx)) < 0) {
        printf("cleanup-test: pipe create 1 failed: %d\n", p1tx);
        return -1;
    }
    _magenta_handle_close(p1rx);
    printf("cleanup-test: about to wait, should return immediately with PEER_CLOSED\n");
    r = _magenta_handle_wait_one(p1tx, MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
                                 MX_TIME_INFINITE, &pending, NULL);
    if (r) {
        printf("cleanup-test: FAILED, error %d\n", r);
        return -1;
    }
    if (pending != MX_SIGNAL_PEER_CLOSED) {
        printf("cleanup-test: FAILED, pending=%x, not PEER_CLOSED\n", pending);
        return -1;
    }
    printf("cleanup-test: SUCCESS, observed PEER_CLOSED signal\n\n");
    _magenta_handle_close(p1tx);
    _magenta_handle_close(p1rx);

    // TEST2
    // Create a pipe, close one end. Then create an event and write a
    // message on the pipe sending the event along. The event normally
    // dissapears from this process handle table but since the message_write
    // fails (because the other end is closed) The event should still
    // be usable from this process.
    test_state = 2;
    if ((p1tx = _magenta_message_pipe_create(&p1rx)) < 0) {
        printf("cleanup-test: pipe create 1 failed: %d\n", p1tx);
        return -1;
    }
    _magenta_handle_close(p1rx);

    mx_handle_t event = _magenta_event_create(0u);
    if (event < 0) {
        printf("cleanup-test: event create failed: %d\n", p1tx);
        return -1;
    }

    r = _magenta_message_write(p1tx, &msg, sizeof(msg), &event, 1, 0);
    if (r != ERR_BAD_STATE) {
        printf("cleanup-test: unexpected message_write return code: %d\n", r);
        return -1;
    }

    if ((r = _magenta_event_signal(event)) < 0) {
        printf("cleanup-test: unable to signal event!\n");
        return -1;
    }

    printf("cleanup-test: SUCCESS, event is alive\n\n");

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
    if ((p0tx = _magenta_message_pipe_create(&p0rx)) < 0) {
        printf("cleanup-test: pipe create 0 failed: %d\n", p0tx);
        return -1;
    }

    if ((p1tx = _magenta_message_pipe_create(&p1rx)) < 0) {
        printf("cleanup-test: pipe create 1 failed: %d\n", p1tx);
        return -1;
    }

    if ((r = _magenta_message_write(p0tx, &msg, sizeof(msg), &p1rx, 1, 0)) < 0) {
        printf("cleanup-test: pipe write failed: %d\n", r);
        return -1;
    }

    _magenta_handle_close(p0tx);
    _magenta_handle_close(p0rx);

    printf("cleanup-test: about to wait, should return immediately with PEER_CLOSED\n");
    r = _magenta_handle_wait_one(p1tx, MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
                                 MX_TIME_INFINITE, &pending, NULL);
    if (r) {
        printf("cleanup-test: FAILED, error %d\n", r);
        return -1;
    }
    if (pending != MX_SIGNAL_PEER_CLOSED) {
        printf("cleanup-test: FAILED, pending=%x, not PEER_CLOSED\n", pending);
        return -1;
    }

    test_state = 100;
    printf("cleanup-test: PASSED\n");
    _magenta_handle_close(p1tx);
    return 0;
}
