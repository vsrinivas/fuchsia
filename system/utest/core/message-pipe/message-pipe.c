// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <magenta/syscalls.h>
#include <unittest/unittest.h>
#include <stdbool.h>
#include <stdio.h>
#include <test-utils/test-utils.h>
#include <unistd.h>

mx_handle_t _pipe[4];

/**
 * Message pipe tests with wait multiple.
 *
 * Tests signal state persistence and various combinations of states on multiple handles.
 *
 * Test sequence (may not be exact due to concurrency):
 *   1. Create 2 pipes and start a reader thread.
 *   2. Reader blocks wait on both pipes.
 *   3. Write to both pipes and yield.
 *   4. Reader wake up with pipe 1 and pipe 2 readable.
 *   5. Reader reads from pipe 1, and calls wait again.
 *   6. Reader should wake up immediately, with pipe 1 not readable and pipe 2 readable.
 *   7. Reader blocks on wait.
 *   8. Write to pipe 1 and yield.
 *   9. Reader wake up with pipe 1 readable and reads from pipe 1.
 *  10. Reader blocks on wait.
 *  11. Write to pipe 2 and close both pipes, then yield.
 *  12. Reader wake up with pipe 2 closed and readable.
 *  13. Read from pipe 2 and wait.
 *  14. Reader wake up with pipe 2 closed, closes both pipes and exit.
 */

static intptr_t reader_thread(void* arg) {
    const unsigned int index = 2;
    mx_handle_t* pipe = &_pipe[index];
    mx_status_t status;
    mx_signals_state_t states[2];
    mx_signals_t signals = MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED;
    unsigned int packets[2] = {0, 0};
    bool closed[2] = {false, false};
    do {
        status = mx_handle_wait_many(2, pipe, &signals, MX_TIME_INFINITE, NULL, states);
        ASSERT_EQ(status, NO_ERROR, "error from mx_handle_wait_many");
        uint32_t data;
        uint32_t num_bytes = sizeof(uint32_t);
        if (states[0].satisfied & MX_SIGNAL_READABLE) {
            status = mx_msgpipe_read(pipe[0], &data, &num_bytes, NULL, 0u, 0u);
            ASSERT_EQ(status, NO_ERROR, "error while reading message");
            packets[0] += 1;
        } else if (states[1].satisfied & MX_SIGNAL_READABLE) {
            status = mx_msgpipe_read(pipe[1], &data, &num_bytes, NULL, 0u, 0u);
            ASSERT_EQ(status, NO_ERROR, "error while reading message");
            packets[1] += 1;
        } else {
            if (states[0].satisfied & MX_SIGNAL_PEER_CLOSED)
                closed[0] = true;
            if (states[1].satisfied & MX_SIGNAL_PEER_CLOSED)
                closed[1] = true;
        }
    } while (!closed[0] || !closed[1]);
    assert(packets[0] == 3);
    assert(packets[1] == 2);
    mx_thread_exit();
    return 0;
}

mx_signals_t get_satisfied_signals(mx_handle_t handle) {
    mx_signals_state_t signals_state = {0};
    mx_status_t status = mx_handle_wait_one(handle, 0u, 0u, &signals_state);
    assert(status == ERR_BAD_STATE);  // "Unsatisfiable".
    return signals_state.satisfied;
}

mx_signals_t get_satisfiable_signals(mx_handle_t handle) {
    mx_signals_state_t signals_state = {0};
    mx_status_t status = mx_handle_wait_one(handle, 0u, 0u, &signals_state);
    assert(status == ERR_BAD_STATE);  // "Unsatisfiable".
    return signals_state.satisfiable;
}

bool message_pipe_test(void) {
    BEGIN_TEST;

    mx_status_t status;

    mx_handle_t h[2];
    status = mx_msgpipe_create(h, 0);
    ASSERT_EQ(status, NO_ERROR, "error in message pipe create");

    ASSERT_EQ(get_satisfied_signals(h[0]), MX_SIGNAL_WRITABLE, "");
    ASSERT_EQ(get_satisfied_signals(h[1]), MX_SIGNAL_WRITABLE, "");
    ASSERT_EQ(get_satisfiable_signals(h[0]),
              MX_SIGNAL_READABLE | MX_SIGNAL_WRITABLE | MX_SIGNAL_PEER_CLOSED, "");
    ASSERT_EQ(get_satisfiable_signals(h[1]),
              MX_SIGNAL_READABLE | MX_SIGNAL_WRITABLE | MX_SIGNAL_PEER_CLOSED, "");

    _pipe[0] = h[0];
    _pipe[2] = h[1];

    static const uint32_t write_data = 0xdeadbeef;
    status = mx_msgpipe_write(_pipe[0], &write_data, sizeof(uint32_t), NULL, 0u, 0u);
    ASSERT_EQ(status, NO_ERROR, "error in message write");
    ASSERT_EQ(get_satisfied_signals(_pipe[0]), MX_SIGNAL_WRITABLE, "");
    ASSERT_EQ(get_satisfied_signals(_pipe[2]), MX_SIGNAL_READABLE | MX_SIGNAL_WRITABLE, "");

    status = mx_msgpipe_create(h, 0);
    ASSERT_EQ(status, NO_ERROR, "error in message pipe create");

    _pipe[1] = h[0];
    _pipe[3] = h[1];

    const char* reader = "reader";
    mx_handle_t thread = tu_thread_create(reader_thread, NULL, reader);
    ASSERT_GE(thread, 0, "error in thread create");

    status = mx_msgpipe_write(_pipe[1], &write_data, sizeof(uint32_t), NULL, 0u, 0u);
    ASSERT_EQ(status, NO_ERROR, "error in message write");

    usleep(1);

    status = mx_msgpipe_write(_pipe[0], &write_data, sizeof(uint32_t), NULL, 0u, 0u);
    ASSERT_EQ(status, NO_ERROR, "error in message write");

    status = mx_msgpipe_write(_pipe[0], &write_data, sizeof(uint32_t), NULL, 0u, 0u);
    ASSERT_EQ(status, NO_ERROR, "error in message write");

    usleep(1);

    status = mx_msgpipe_write(_pipe[1], &write_data, sizeof(uint32_t), NULL, 0u, 0u);
    ASSERT_EQ(status, NO_ERROR, "error in message write");

    mx_handle_close(_pipe[1]);
    // The reader thread is reading from _pipe[3], so we may or may not have "readable".
    ASSERT_TRUE((get_satisfied_signals(_pipe[3]) & MX_SIGNAL_PEER_CLOSED), "");
    ASSERT_TRUE((get_satisfiable_signals(_pipe[3]) & MX_SIGNAL_PEER_CLOSED), "");

    usleep(1);
    mx_handle_close(_pipe[0]);

    mx_handle_wait_one(thread, MX_SIGNAL_SIGNALED, MX_TIME_INFINITE, NULL);

    // Since the the other side of _pipe[3] is closed, and the read thread read everything from it,
    // the only satisfied/satisfiable signals should be "peer closed".
    ASSERT_EQ(get_satisfied_signals(_pipe[3]), MX_SIGNAL_PEER_CLOSED, "");
    ASSERT_EQ(get_satisfiable_signals(_pipe[3]), MX_SIGNAL_PEER_CLOSED, "");

    mx_handle_close(_pipe[2]);
    mx_handle_close(_pipe[3]);

    END_TEST;
}

bool message_pipe_read_error_test(void) {
    BEGIN_TEST;
    mx_handle_t pipe[2];
    mx_status_t status = mx_msgpipe_create(pipe, 0);
    ASSERT_EQ(status, NO_ERROR, "error in message pipe create");

    // Read from an empty message pipe.
    status = mx_msgpipe_read(pipe[0], NULL, 0u, NULL, 0u, 0u);
    ASSERT_EQ(status, ERR_BAD_STATE, "read on empty non-closed pipe produced incorrect error");

    char data = 'x';
    status = mx_msgpipe_write(pipe[1], &data, 1u, NULL, 0u, 0u);
    ASSERT_EQ(status, NO_ERROR, "write failed");

    mx_handle_close(pipe[1]);

    // Read a message with the peer closed, should yield the message.
    char read_data = '\0';
    uint32_t read_data_size = 1u;
    status = mx_msgpipe_read(pipe[0], &read_data, &read_data_size, NULL, 0u, 0u);
    ASSERT_EQ(status, NO_ERROR, "read failed with peer closed but message in the pipe");
    ASSERT_EQ(read_data_size, 1u, "read returned incorrect number of bytes");
    ASSERT_EQ(read_data, 'x', "read returned incorrect data");

    // Read from an empty pipe with a closed peer, should yield a channel closed error.
    status = mx_msgpipe_read(pipe[0], NULL, 0u, NULL, 0u, 0u);
    ASSERT_EQ(status, ERR_REMOTE_CLOSED, "read on empty closed pipe produced incorrect error");

    // Waiting for readability should yield a bad state error.
    status = mx_handle_wait_one(pipe[0], MX_SIGNAL_READABLE, 0u, NULL);
    ASSERT_EQ(status, ERR_BAD_STATE, "waiting for readability should not succeed");

    END_TEST;
}

bool message_pipe_close_test(void) {
    BEGIN_TEST;
    mx_handle_t pipe[2];
    ASSERT_EQ(mx_msgpipe_create(pipe, 0), NO_ERROR, "");
    mx_handle_t pipe1[2];
    ASSERT_EQ(mx_msgpipe_create(pipe1, 0), NO_ERROR, "");
    mx_handle_t pipe2[2];
    ASSERT_EQ(mx_msgpipe_create(pipe2, 0), NO_ERROR, "");

    // Write pipe1[0] to pipe[0] (to be received by pipe[1]) and pipe2[0] to pipe[1] (to be received
    // by pipe[0]).
    ASSERT_EQ(mx_msgpipe_write(pipe[0], NULL, 0u, &pipe1[0], 1u, 0u), NO_ERROR, "");
    pipe1[0] = MX_HANDLE_INVALID;
    ASSERT_EQ(mx_msgpipe_write(pipe[1], NULL, 0u, &pipe2[0], 1u, 0u), NO_ERROR, "");
    pipe2[0] = MX_HANDLE_INVALID;

    // Close pipe[1]; the former pipe1[0] should be closed, so pipe1[1] should have peer closed.
    ASSERT_EQ(mx_handle_close(pipe[1]), NO_ERROR, "");
    pipe[1] = MX_HANDLE_INVALID;
    ASSERT_EQ(get_satisfied_signals(pipe1[1]), MX_SIGNAL_PEER_CLOSED, "");
    ASSERT_EQ(get_satisfiable_signals(pipe1[1]), MX_SIGNAL_PEER_CLOSED, "");
    ASSERT_EQ(get_satisfied_signals(pipe2[1]), MX_SIGNAL_WRITABLE, "");
    ASSERT_EQ(get_satisfiable_signals(pipe2[1]),
              MX_SIGNAL_READABLE | MX_SIGNAL_WRITABLE | MX_SIGNAL_PEER_CLOSED, "");

    // Close pipe[0]; the former pipe2[0] should be closed, so pipe2[1] should have peer closed.
    ASSERT_EQ(mx_handle_close(pipe[0]), NO_ERROR, "");
    pipe[0] = MX_HANDLE_INVALID;
    ASSERT_EQ(get_satisfied_signals(pipe1[1]), MX_SIGNAL_PEER_CLOSED, "");
    ASSERT_EQ(get_satisfiable_signals(pipe1[1]), MX_SIGNAL_PEER_CLOSED, "");
    ASSERT_EQ(get_satisfied_signals(pipe2[1]), MX_SIGNAL_PEER_CLOSED, "");
    ASSERT_EQ(get_satisfiable_signals(pipe2[1]), MX_SIGNAL_PEER_CLOSED, "");

    ASSERT_EQ(mx_handle_close(pipe1[1]), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(pipe2[1]), NO_ERROR, "");

    END_TEST;
}

bool message_pipe_non_transferable(void) {
    BEGIN_TEST;

    mx_handle_t pipe[2];
    ASSERT_EQ(mx_msgpipe_create(pipe, 0), NO_ERROR, "");
    mx_handle_t event = mx_event_create(0u);
    ASSERT_GT(event, 0, "failed to create event");
    mx_handle_basic_info_t event_handle_info;
    mx_ssize_t get_info_result = mx_handle_get_info(event, MX_INFO_HANDLE_BASIC, &event_handle_info,
                                                    sizeof(event_handle_info));
    ASSERT_EQ(get_info_result, (mx_ssize_t)sizeof(event_handle_info), "failed to get event info");
    mx_rights_t initial_event_rights = event_handle_info.rights;
    mx_handle_t non_transferable_event =
            mx_handle_duplicate(event, initial_event_rights & ~MX_RIGHT_TRANSFER);

    mx_status_t write_result = mx_msgpipe_write(pipe[0], NULL, 0, &non_transferable_event, 1u, 0u);
    EXPECT_EQ(write_result, ERR_ACCESS_DENIED, "message_write should fail with ACCESS_DENIED");

    mx_status_t close_result = mx_handle_close(non_transferable_event);
    EXPECT_EQ(close_result, NO_ERROR, "");

    END_TEST;
}

bool message_pipe_duplicate_handles(void) {
    BEGIN_TEST;

    mx_handle_t pipe[2];
    ASSERT_EQ(mx_msgpipe_create(pipe, 0), NO_ERROR, "");

    mx_handle_t event = mx_event_create(0u);
    ASSERT_GT(event, 0, "failed to create event");

    mx_handle_t dup_handles[2] = { event, event };
    mx_status_t write_result = mx_msgpipe_write(pipe[0], NULL, 0, dup_handles, 2u, 0u);
    EXPECT_EQ(write_result, ERR_INVALID_ARGS, "message_write should fail with ERR_INVALID_ARGS");

    mx_status_t close_result = mx_handle_close(event);
    EXPECT_EQ(close_result, NO_ERROR, "");
    close_result = mx_handle_close(pipe[0]);
    EXPECT_EQ(close_result, NO_ERROR, "");
    close_result = mx_handle_close(pipe[1]);
    EXPECT_EQ(close_result, NO_ERROR, "");

    END_TEST;
}

BEGIN_TEST_CASE(message_pipe_tests)
RUN_TEST(message_pipe_test)
RUN_TEST(message_pipe_read_error_test)
RUN_TEST(message_pipe_close_test)
RUN_TEST(message_pipe_non_transferable)
RUN_TEST(message_pipe_duplicate_handles)
END_TEST_CASE(message_pipe_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
