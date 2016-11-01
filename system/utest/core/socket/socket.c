// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <magenta/syscalls.h>
#include <unittest/unittest.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static mx_signals_t get_satisfied_signals(mx_handle_t handle) {
    mx_signals_t pending = 0;
    mx_handle_wait_one(handle, 0u, 0u, &pending);
    return pending;
}

static bool socket_basic(void) {
    BEGIN_TEST;

    mx_status_t status;
    mx_size_t count;

    mx_handle_t h[2];
    uint32_t read_data[] = { 0, 0 };

    status = mx_socket_create(0, h, h + 1);
    ASSERT_EQ(status, NO_ERROR, "");

    status = mx_socket_read(h[0], 0u, read_data, sizeof(read_data), &count);
    ASSERT_EQ(status, ERR_SHOULD_WAIT, "");

    static const uint32_t write_data[] = { 0xdeadbeef, 0xc0ffee };
    status = mx_socket_write(h[0], 0u, &write_data[0], sizeof(write_data[0]), &count);
    ASSERT_EQ(status, NO_ERROR, "");
    ASSERT_EQ(count, sizeof(write_data[0]), "");
    status = mx_socket_write(h[0], 0u, &write_data[1], sizeof(write_data[1]), &count);
    ASSERT_EQ(status, NO_ERROR, "");
    ASSERT_EQ(count, sizeof(write_data[1]), "");

    status = mx_socket_read(h[1], 0u, read_data, sizeof(read_data), &count);
    ASSERT_EQ(status, NO_ERROR, "");
    ASSERT_EQ(count, sizeof(read_data), "");
    ASSERT_EQ(read_data[0], write_data[0], "");
    ASSERT_EQ(read_data[1], write_data[1], "");

    mx_handle_close(h[1]);

    status = mx_socket_write(h[0], 0u, &write_data[1], sizeof(write_data[1]), &count);
    ASSERT_EQ(status, ERR_REMOTE_CLOSED, "");

    mx_handle_close(h[0]);
    END_TEST;
}

static bool socket_signals(void) {
    BEGIN_TEST;

    mx_status_t status;
    mx_size_t count;

    mx_handle_t h0, h1;
    status = mx_socket_create(0, &h0, &h1);
    ASSERT_EQ(status, NO_ERROR, "");

    mx_signals_t signals0 = get_satisfied_signals(h0);
    mx_signals_t signals1 = get_satisfied_signals(h1);

    ASSERT_EQ(signals0, MX_SIGNAL_WRITABLE, "");
    ASSERT_EQ(signals1, MX_SIGNAL_WRITABLE, "");

    const mx_size_t kAllSize = 128 * 1024;
    char* big_buf =  (char*) malloc(kAllSize);
    ASSERT_NONNULL(big_buf, "");

    memset(big_buf, 0x66, kAllSize);

    status = mx_socket_write(h0, 0u, big_buf, kAllSize / 16, &count);
    ASSERT_EQ(status, NO_ERROR, "");
    ASSERT_EQ(count, kAllSize / 16, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);

    ASSERT_EQ(signals0, MX_SIGNAL_WRITABLE, "");
    ASSERT_EQ(signals1, MX_SIGNAL_READABLE | MX_SIGNAL_WRITABLE, "");

    status = mx_socket_read(h1, 0u, big_buf, kAllSize, &count);
    ASSERT_EQ(status, NO_ERROR, "");
    ASSERT_EQ(count, kAllSize / 16, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);

    ASSERT_EQ(signals0, MX_SIGNAL_WRITABLE, "");
    ASSERT_EQ(signals1, MX_SIGNAL_WRITABLE, "");

    status = mx_object_signal(h0, MX_SIGNAL_WRITABLE, 0u);
    ASSERT_EQ(status, ERR_INVALID_ARGS, "");

    status = mx_object_signal(h0, 0u, MX_SIGNAL_SIGNAL1);
    ASSERT_EQ(status, NO_ERROR, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);

    ASSERT_EQ(signals0, MX_SIGNAL_WRITABLE, "");
    ASSERT_EQ(signals1, MX_SIGNAL_WRITABLE | MX_SIGNAL_SIGNAL1, "");

    mx_handle_close(h1);

    signals0 = get_satisfied_signals(h0);
    ASSERT_EQ(signals0, MX_SIGNAL_PEER_CLOSED, "");

    mx_handle_close(h0);

    free(big_buf);
    END_TEST;
}

static bool socket_half_close(void) {
    BEGIN_TEST;

    mx_status_t status;
    mx_size_t count;
    mx_signals_t signals0, signals1;

    mx_handle_t h0, h1;
    status = mx_socket_create(0, &h0, &h1);
    ASSERT_EQ(status, NO_ERROR, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);

    ASSERT_EQ(signals0, MX_SIGNAL_WRITABLE, "");
    ASSERT_EQ(signals1, MX_SIGNAL_WRITABLE, "");

    status = mx_socket_write(h1, 0u, "12345", 5u, &count);
    ASSERT_EQ(status, NO_ERROR, "");
    ASSERT_EQ(count, 5u, "");

    status = mx_socket_write(h1, MX_SOCKET_HALF_CLOSE, NULL, 0u, NULL);
    ASSERT_EQ(status, NO_ERROR, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);

    ASSERT_EQ(signals0, MX_SIGNAL_WRITABLE | MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED, "");
    ASSERT_EQ(signals1, 0u, "");

    status = mx_socket_write(h0, 0u, "abcde", 5u, &count);
    ASSERT_EQ(status, NO_ERROR, "");
    ASSERT_EQ(count, 5u, "");

    signals1 = get_satisfied_signals(h1);
    ASSERT_EQ(signals1, MX_SIGNAL_READABLE, "");

    status = mx_socket_write(h1, 0u, "fghij", 5u, &count);
    ASSERT_EQ(status, ERR_BAD_STATE, "");

    char rbuf[10] = {0};

    status = mx_socket_read(h0, 0u, rbuf, sizeof(rbuf), &count);
    ASSERT_EQ(status, NO_ERROR, "");
    ASSERT_EQ(count, 5u, "");
    ASSERT_EQ(memcmp(rbuf, "12345", 5), 0, "");

    status = mx_socket_read(h0, 0u, rbuf, 1u, &count);
    ASSERT_EQ(status, ERR_REMOTE_CLOSED, "");

    signals0 = get_satisfied_signals(h0);
    ASSERT_EQ(signals0, MX_SIGNAL_WRITABLE | MX_SIGNAL_PEER_CLOSED, "");

    status = mx_socket_read(h1, 0u, rbuf, sizeof(rbuf), &count);
    ASSERT_EQ(status, NO_ERROR, "");
    ASSERT_EQ(count, 5u, "");
    ASSERT_EQ(memcmp(rbuf, "abcde", 5), 0, "");

    mx_handle_close(h0);
    mx_handle_close(h1);

    END_TEST;
}

BEGIN_TEST_CASE(socket_tests)
RUN_TEST(socket_basic)
RUN_TEST(socket_signals)
RUN_TEST(socket_half_close)
END_TEST_CASE(socket_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
