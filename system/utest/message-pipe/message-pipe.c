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

#include <assert.h>
#include <mojo/mojo.h>
#include <mojo/mojo_message_pipe.h>
#include <mojo/mojo_threads.h>
#include <mxu/unittest.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

mojo_handle_t _pipe[4];

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

static int reader_thread(void* arg) {
    unsigned int index = 2;
    mojo_handle_t* pipe = &_pipe[index];
    mojo_result_t result;
    mojo_handle_signals_t satisfied[2], satisfiable[2];
    mojo_handle_signals_t signals = MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED;
    unsigned int packets[2] = {0, 0};
    bool closed[2] = {false, false};
    do {
        result = mojo_wait(pipe, &signals, 2, NULL, MOJO_DEADLINE_INDEFINITE, satisfied, satisfiable);
        ASSERT_EQ(result, MOJO_RESULT_OK, "error from mojo_wait");
        uint32_t data;
        uint32_t num_bytes = sizeof(uint32_t);
        if (satisfied[0] & MOJO_HANDLE_SIGNAL_READABLE) {
            result = mojo_read_message(pipe[0], &data, &num_bytes, NULL, 0, 0);
            ASSERT_EQ(result, MOJO_RESULT_OK, "error while reading message");
            packets[0] += 1;
        } else if (satisfied[1] & MOJO_HANDLE_SIGNAL_READABLE) {
            result = mojo_read_message(pipe[1], &data, &num_bytes, NULL, 0, 0);
            ASSERT_EQ(result, MOJO_RESULT_OK, "error while reading message");
            packets[1] += 1;
        } else {
            if (satisfied[0] & MOJO_HANDLE_SIGNAL_PEER_CLOSED)
                closed[0] = true;
            if (satisfied[1] & MOJO_HANDLE_SIGNAL_PEER_CLOSED)
                closed[1] = true;
        }
    } while (!closed[0] || !closed[1]);
    mojo_close(pipe[0]);
    mojo_close(pipe[1]);
    assert(packets[0] == 3);
    assert(packets[1] == 2);
    return 0;
}

bool message_pipe_test(void) {
    BEGIN_TEST;
    mojo_result_t result = mojo_create_message_pipe(&_pipe[0], &_pipe[2]);
    ASSERT_EQ(result, MOJO_RESULT_OK, "error in create message pipe");

    result = mojo_create_message_pipe(&_pipe[1], &_pipe[3]);
    ASSERT_EQ(result, MOJO_RESULT_OK, "error in create message pipe");

    mojo_handle_t thread;
    result = mojo_thread_create(reader_thread, NULL, &thread, "reader");
    ASSERT_EQ(result, MOJO_RESULT_OK, "error in mojo_thread_create");

    uint32_t data = 0xdeadbeef;
    result = mojo_write_message(_pipe[0], &data, sizeof(uint32_t), NULL, 0, 0);
    ASSERT_EQ(result, MOJO_RESULT_OK, "error in mojo_write_message");

    result = mojo_write_message(_pipe[1], &data, sizeof(uint32_t), NULL, 0, 0);
    ASSERT_EQ(result, MOJO_RESULT_OK, "error in mojo_write_message");

    usleep(1);

    result = mojo_write_message(_pipe[0], &data, sizeof(uint32_t), NULL, 0, 0);
    ASSERT_EQ(result, MOJO_RESULT_OK, "error in mojo_write_message");

    result = mojo_write_message(_pipe[0], &data, sizeof(uint32_t), NULL, 0, 0);
    ASSERT_EQ(result, MOJO_RESULT_OK, "error in mojo_write_message");

    usleep(1);

    result = mojo_write_message(_pipe[1], &data, sizeof(uint32_t), NULL, 0, 0);
    ASSERT_EQ(result, MOJO_RESULT_OK, "error in mojo_write_message");

    mojo_close(_pipe[1]);

    usleep(1);
    mojo_close(_pipe[0]);

    mojo_thread_join(thread, MOJO_DEADLINE_INDEFINITE);

    END_TEST;
}

bool message_pipe_read_error_test(void) {
    BEGIN_TEST;
    mojo_handle_t pipe[2];
    mojo_result_t result = mojo_create_message_pipe(&pipe[0], &pipe[1]);
    ASSERT_EQ(result, MOJO_RESULT_OK, "error creating message pipe");

    // Read from an empty message pipe.
    result = mojo_read_message(pipe[0], NULL, NULL, NULL, NULL, 0u);
    ASSERT_EQ(result, MOJO_RESULT_FAILED_PRECONDITION, "read on empty non-closed pipe produced incorrect error");

    char data = 'x';
    result = mojo_write_message(pipe[1], &data, 1u, NULL, 0u, 0u);
    ASSERT_EQ(result, MOJO_RESULT_OK, "write failed");

    mojo_close(pipe[1]);

    // Read a message with the peer closed, should yield the message.
    char read_data = '\0';
    uint32_t read_data_size = 1u;
    result = mojo_read_message(pipe[0], &read_data, &read_data_size, NULL, NULL, 0u);
    ASSERT_EQ(result, MOJO_RESULT_OK, "read failed with peer closed but message in the pipe");
    ASSERT_EQ(read_data_size, 1u, "read returned incorrect number of bytes");
    ASSERT_EQ(read_data, 'x', "read returned incorrect data");

    // Read from an empty pipe with a closed peer, should yield a channel closed error.
    result = mojo_read_message(pipe[0], NULL, 0u, NULL, 0u, 0u);
    // TODO: This error code should be distinguishable from reading from an empty pipe with an open
    // peer.
    ASSERT_EQ(result, MOJO_RESULT_FAILED_PRECONDITION, "read on empty closed pipe produced incorrect error");

    END_TEST;
}

BEGIN_TEST_CASE(message_pipe_tests)
RUN_TEST(message_pipe_test)
RUN_TEST(message_pipe_read_error_test)
END_TEST_CASE(message_pipe_tests)

int main(void) {
    return unittest_run_all_tests() ? 0 : -1;
}
