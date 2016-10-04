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
#include <magenta/syscalls.h>
#include <unittest/unittest.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static mx_signals_t get_satisfied_signals(mx_handle_t handle) {
    mx_signals_state_t signals_state = {0};
    mx_handle_wait_one(handle, 0u, 0u, &signals_state);
    return signals_state.satisfied;
}

static bool socket_basic(void) {
    BEGIN_TEST;

    mx_status_t status;
    mx_ssize_t ssize;

    mx_handle_t h[2];
    uint32_t read_data[] = { 0, 0 };

    status = mx_socket_create(h, 0);
    ASSERT_EQ(status, NO_ERROR, "");

    ssize = mx_socket_read(h[0], 0u, sizeof(read_data), read_data);
    ASSERT_EQ(ssize, ERR_SHOULD_WAIT, "");

    static const uint32_t write_data[] = { 0xdeadbeef, 0xc0ffee };
    ssize = mx_socket_write(h[0], 0u, sizeof(write_data[0]), &write_data[0]);
    ASSERT_EQ(ssize, (int)sizeof(write_data[0]), "");
    ssize = mx_socket_write(h[0], 0u, sizeof(write_data[1]), &write_data[1]);
    ASSERT_EQ(ssize, (int)sizeof(write_data[1]), "");

    ssize = mx_socket_read(h[1], 0u, sizeof(read_data), read_data);
    ASSERT_EQ(ssize, (int)sizeof(read_data), "");
    ASSERT_EQ(read_data[0], write_data[0], "");
    ASSERT_EQ(read_data[1], write_data[1], "");

    mx_handle_close(h[1]);

    ssize = mx_socket_write(h[0], 0u, sizeof(write_data[1]), &write_data[1]);
    ASSERT_EQ(ssize, ERR_REMOTE_CLOSED, "");

    mx_handle_close(h[0]);
    END_TEST;
}

static bool socket_signals(void) {
    BEGIN_TEST;

    mx_status_t status;
    mx_ssize_t ssize;

    mx_handle_t h[2];
    status = mx_socket_create(h, 0);
    ASSERT_EQ(status, NO_ERROR, "");

    mx_signals_t signals0 = get_satisfied_signals(h[0]);
    mx_signals_t signals1 = get_satisfied_signals(h[1]);

    ASSERT_EQ(signals0, MX_SIGNAL_WRITABLE, "");
    ASSERT_EQ(signals1, MX_SIGNAL_WRITABLE, "");

    const int kAllSize = 128 * 1024;
    char* big_buf =  (char*) malloc(kAllSize);
    ASSERT_NEQ(big_buf, NULL, "");

    memset(big_buf, 0x66, kAllSize);

    ssize = mx_socket_write(h[0], 0u, kAllSize / 16, big_buf);
    ASSERT_EQ(ssize, kAllSize / 16, "");

    signals0 = get_satisfied_signals(h[0]);
    signals1 = get_satisfied_signals(h[1]);

    ASSERT_EQ(signals0, MX_SIGNAL_WRITABLE, "");
    ASSERT_EQ(signals1, MX_SIGNAL_READABLE | MX_SIGNAL_WRITABLE, "");

    ssize = mx_socket_read(h[1], 0u, kAllSize, big_buf);
    ASSERT_EQ(ssize, kAllSize / 16, "");

    signals0 = get_satisfied_signals(h[0]);
    signals1 = get_satisfied_signals(h[1]);

    ASSERT_EQ(signals0, MX_SIGNAL_WRITABLE, "");
    ASSERT_EQ(signals1, MX_SIGNAL_WRITABLE, "");

    status = mx_object_signal(h[0], MX_SIGNAL_WRITABLE, 0u);
    ASSERT_EQ(status, ERR_INVALID_ARGS, "");

    status = mx_object_signal(h[0], 0u, MX_SIGNAL_SIGNAL1);
    ASSERT_EQ(status, NO_ERROR, "");

    signals0 = get_satisfied_signals(h[0]);
    signals1 = get_satisfied_signals(h[1]);

    ASSERT_EQ(signals0, MX_SIGNAL_WRITABLE, "");
    ASSERT_EQ(signals1, MX_SIGNAL_WRITABLE | MX_SIGNAL_SIGNAL1, "");

    mx_handle_close(h[1]);

    signals0 = get_satisfied_signals(h[0]);
    ASSERT_EQ(signals0, MX_SIGNAL_PEER_CLOSED, "");

    mx_handle_close(h[0]);

    free(big_buf);
    END_TEST;
}

static bool socket_oob(void) {
    BEGIN_TEST;

    mx_status_t status;
    mx_ssize_t ssize;
    mx_signals_t signals[2];

    mx_handle_t h[2];
    status = mx_socket_create(h, 0);
    ASSERT_EQ(status, NO_ERROR, "");

    static const uint32_t write_data[] = { 0xdeadbeef, 0xc0ffee };
    ssize = mx_socket_write(h[0], MX_SOCKET_CONTROL, sizeof(write_data), &write_data);
    ASSERT_EQ(ssize, (int)sizeof(write_data), "");

    ssize = mx_socket_write(h[0], MX_SOCKET_CONTROL, sizeof(write_data[1]), &write_data[1]);
    ASSERT_EQ(ssize, ERR_SHOULD_WAIT, "");

    signals[0] = get_satisfied_signals(h[0]);
    signals[1] = get_satisfied_signals(h[1]);

    ASSERT_EQ(signals[0], MX_SIGNAL_WRITABLE, "");
    ASSERT_EQ(signals[1], MX_SIGNAL_WRITABLE | MX_SIGNAL_SIGNALED, "");

    uint32_t read_data[4];
    ssize = mx_socket_read(h[0], MX_SOCKET_CONTROL, sizeof(read_data), read_data);
    ASSERT_EQ(ssize, ERR_SHOULD_WAIT, "");

    signals[0] = get_satisfied_signals(h[0]);
    signals[1] = get_satisfied_signals(h[1]);

    ASSERT_EQ(signals[0], MX_SIGNAL_WRITABLE, "");
    ASSERT_EQ(signals[1], MX_SIGNAL_WRITABLE | MX_SIGNAL_SIGNALED, "");

    ssize = mx_socket_read(h[1], MX_SOCKET_CONTROL, sizeof(read_data) + 10, read_data);
    ASSERT_EQ(ssize, (int)sizeof(write_data), "");

    ASSERT_EQ(read_data[0], write_data[0], "");

    signals[0] = get_satisfied_signals(h[0]);
    signals[1] = get_satisfied_signals(h[1]);

    ASSERT_EQ(signals[0], MX_SIGNAL_WRITABLE, "");
    ASSERT_EQ(signals[1], MX_SIGNAL_WRITABLE, "");

    ssize = mx_socket_read(h[1], MX_SOCKET_CONTROL, sizeof(read_data) + 10, read_data);
    ASSERT_EQ(ssize, ERR_SHOULD_WAIT, "");

    mx_handle_close(h[0]);
    mx_handle_close(h[1]);

    END_TEST;
}

static bool socket_half_close(void) {
    BEGIN_TEST;

    mx_status_t status;
    mx_ssize_t ssize;
    mx_signals_t signals[2];

    mx_handle_t h[2];
    status = mx_socket_create(h, 0);
    ASSERT_EQ(status, NO_ERROR, "");

    signals[0] = get_satisfied_signals(h[0]);
    signals[1] = get_satisfied_signals(h[1]);

    ASSERT_EQ(signals[0], MX_SIGNAL_WRITABLE, "");
    ASSERT_EQ(signals[1], MX_SIGNAL_WRITABLE, "");

    ssize = mx_socket_write(h[1], 0u, 5u, "12345");
    ASSERT_EQ(ssize, 5, "");

    ssize = mx_socket_write(h[1], MX_SOCKET_HALF_CLOSE, 0u, NULL);
    ASSERT_EQ(ssize, NO_ERROR, "");

    signals[0] = get_satisfied_signals(h[0]);
    signals[1] = get_satisfied_signals(h[1]);

    ASSERT_EQ(signals[0], MX_SIGNAL_WRITABLE | MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED, "");
    ASSERT_EQ(signals[1], 0u, "");

    ssize = mx_socket_write(h[0], 0u, 5u, "abcde");
    ASSERT_EQ(ssize, 5, "");

    signals[1] = get_satisfied_signals(h[1]);
    ASSERT_EQ(signals[1], MX_SIGNAL_READABLE, "");

    ssize = mx_socket_write(h[1], 0u, 5u, "fghij");
    ASSERT_EQ(ssize, ERR_BAD_STATE, "");

    char rbuf[10] = {0};

    ssize = mx_socket_read(h[0], 0u, sizeof(rbuf), rbuf);
    ASSERT_EQ(ssize, 5, "");
    ASSERT_EQ(memcmp(rbuf, "12345", 5), 0, "");

    ssize = mx_socket_read(h[0], 0u, 1u, rbuf);
    ASSERT_EQ(ssize, ERR_REMOTE_CLOSED, "");

    signals[0] = get_satisfied_signals(h[0]);
    ASSERT_EQ(signals[0], MX_SIGNAL_WRITABLE | MX_SIGNAL_PEER_CLOSED, "");

    ssize = mx_socket_read(h[1], 0u, sizeof(rbuf), rbuf);
    ASSERT_EQ(ssize, 5, "");
    ASSERT_EQ(memcmp(rbuf, "abcde", 5), 0, "");

    mx_handle_close(h[0]);
    mx_handle_close(h[1]);

    END_TEST;
}

BEGIN_TEST_CASE(socket_tests)
RUN_TEST(socket_basic)
RUN_TEST(socket_signals)
RUN_TEST(socket_oob)
RUN_TEST(socket_half_close)
END_TEST_CASE(socket_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
