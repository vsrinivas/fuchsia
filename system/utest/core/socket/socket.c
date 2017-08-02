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
    mx_object_wait_one(handle, 0u, 0u, &pending);
    return pending;
}

static bool socket_basic(void) {
    BEGIN_TEST;

    mx_status_t status;
    size_t count;

    mx_handle_t h[2];
    uint32_t read_data[] = { 0, 0 };

    status = mx_socket_create(0, h, h + 1);
    ASSERT_EQ(status, MX_OK, "");

    status = mx_socket_read(h[0], 0u, read_data, sizeof(read_data), &count);
    EXPECT_EQ(status, MX_ERR_SHOULD_WAIT, "");

    static const uint32_t write_data[] = { 0xdeadbeef, 0xc0ffee };
    status = mx_socket_write(h[0], 0u, &write_data[0], sizeof(write_data[0]), &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, sizeof(write_data[0]), "");
    status = mx_socket_write(h[0], 0u, &write_data[1], sizeof(write_data[1]), &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, sizeof(write_data[1]), "");

    status = mx_socket_read(h[1], 0u, read_data, sizeof(read_data), &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, sizeof(read_data), "");
    EXPECT_EQ(read_data[0], write_data[0], "");
    EXPECT_EQ(read_data[1], write_data[1], "");

    status = mx_socket_write(h[0], 0u, write_data, sizeof(write_data), NULL);
    EXPECT_EQ(status, MX_OK, "");
    memset(read_data, 0, sizeof(read_data));
    status = mx_socket_read(h[1], 0u, read_data, sizeof(read_data), NULL);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(read_data[0], write_data[0], "");
    EXPECT_EQ(read_data[1], write_data[1], "");

    mx_handle_close(h[1]);

    status = mx_socket_write(h[0], 0u, &write_data[1], sizeof(write_data[1]), &count);
    EXPECT_EQ(status, MX_ERR_PEER_CLOSED, "");

    mx_handle_close(h[0]);
    END_TEST;
}

static bool socket_signals(void) {
    BEGIN_TEST;

    mx_status_t status;
    size_t count;

    mx_handle_t h0, h1;
    status = mx_socket_create(0, &h0, &h1);
    ASSERT_EQ(status, MX_OK, "");

    mx_signals_t signals0 = get_satisfied_signals(h0);
    mx_signals_t signals1 = get_satisfied_signals(h1);

    EXPECT_EQ(signals0, MX_SOCKET_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");
    EXPECT_EQ(signals1, MX_SOCKET_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");

    const size_t kAllSize = 128 * 1024;
    char* big_buf =  (char*) malloc(kAllSize);
    ASSERT_NONNULL(big_buf, "");

    memset(big_buf, 0x66, kAllSize);

    status = mx_socket_write(h0, 0u, big_buf, kAllSize / 16, &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, kAllSize / 16, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);

    EXPECT_EQ(signals0, MX_SOCKET_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");
    EXPECT_EQ(signals1, MX_SOCKET_READABLE | MX_SOCKET_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");

    status = mx_socket_read(h1, 0u, big_buf, kAllSize, &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, kAllSize / 16, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);

    EXPECT_EQ(signals0, MX_SOCKET_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");
    EXPECT_EQ(signals1, MX_SOCKET_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");

    status = mx_object_signal_peer(h0, MX_SOCKET_WRITABLE, 0u);
    EXPECT_EQ(status, MX_ERR_INVALID_ARGS, "");

    status = mx_object_signal_peer(h0, 0u, MX_USER_SIGNAL_1);
    EXPECT_EQ(status, MX_OK, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);

    EXPECT_EQ(signals0, MX_SOCKET_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");
    EXPECT_EQ(signals1, MX_SOCKET_WRITABLE | MX_USER_SIGNAL_1 | MX_SIGNAL_LAST_HANDLE, "");

    mx_handle_close(h1);

    signals0 = get_satisfied_signals(h0);
    EXPECT_EQ(signals0, MX_SOCKET_PEER_CLOSED | MX_SIGNAL_LAST_HANDLE, "");

    mx_handle_close(h0);

    free(big_buf);
    END_TEST;
}

static bool socket_shutdown_write(void) {
    BEGIN_TEST;

    mx_status_t status;
    size_t count;
    mx_signals_t signals0, signals1;

    mx_handle_t h0, h1;
    status = mx_socket_create(0, &h0, &h1);
    ASSERT_EQ(status, MX_OK, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);

    EXPECT_EQ(signals0, MX_SOCKET_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");
    EXPECT_EQ(signals1, MX_SOCKET_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");

    status = mx_socket_write(h1, 0u, "12345", 5u, &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 5u, "");

    status = mx_socket_write(h1, MX_SOCKET_SHUTDOWN_WRITE, NULL, 0u, NULL);
    EXPECT_EQ(status, MX_OK, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);

    EXPECT_EQ(signals0,
        MX_SOCKET_WRITABLE | MX_SOCKET_READABLE | MX_SIGNAL_LAST_HANDLE,
        "");
    EXPECT_EQ(signals1, MX_SOCKET_WRITE_DISABLED | MX_SIGNAL_LAST_HANDLE, "");

    status = mx_socket_write(h0, 0u, "abcde", 5u, &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 5u, "");

    signals1 = get_satisfied_signals(h1);
    EXPECT_EQ(signals1, MX_SOCKET_READABLE | MX_SOCKET_WRITE_DISABLED | MX_SIGNAL_LAST_HANDLE, "");

    status = mx_socket_write(h1, 0u, "fghij", 5u, &count);
    EXPECT_EQ(status, MX_ERR_BAD_STATE, "");

    char rbuf[10] = {0};

    status = mx_socket_read(h0, 0u, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 5u, "");
    EXPECT_EQ(memcmp(rbuf, "12345", 5), 0, "");

    status = mx_socket_read(h0, 0u, rbuf, 1u, &count);
    EXPECT_EQ(status, MX_ERR_BAD_STATE, "");

    signals0 = get_satisfied_signals(h0);
    EXPECT_EQ(signals0, MX_SOCKET_WRITABLE | MX_SOCKET_READ_DISABLED | MX_SIGNAL_LAST_HANDLE, "");

    status = mx_socket_read(h1, 0u, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 5u, "");
    EXPECT_EQ(memcmp(rbuf, "abcde", 5), 0, "");

    mx_handle_close(h0);

    // Calling shutdown after the peer is closed is completely valid.
    status = mx_socket_write(h1, MX_SOCKET_SHUTDOWN_READ, NULL, 0u, NULL);
    EXPECT_EQ(status, MX_OK, "");

    signals1 = get_satisfied_signals(h1);
    EXPECT_EQ(signals1, MX_SOCKET_READ_DISABLED | MX_SOCKET_WRITE_DISABLED | MX_SOCKET_PEER_CLOSED | MX_SIGNAL_LAST_HANDLE, "");

    mx_handle_close(h1);

    END_TEST;
}

static bool socket_shutdown_read(void) {
    BEGIN_TEST;

    mx_status_t status;
    size_t count;
    mx_signals_t signals0, signals1;

    mx_handle_t h0, h1;
    status = mx_socket_create(0, &h0, &h1);
    ASSERT_EQ(status, MX_OK, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);

    EXPECT_EQ(signals0, MX_SOCKET_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");
    EXPECT_EQ(signals1, MX_SOCKET_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");

    status = mx_socket_write(h1, 0u, "12345", 5u, &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 5u, "");

    status = mx_socket_write(h0, MX_SOCKET_SHUTDOWN_READ, NULL, 0u, NULL);
    EXPECT_EQ(status, MX_OK, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);

    EXPECT_EQ(signals0, MX_SOCKET_WRITABLE | MX_SOCKET_READABLE | MX_SIGNAL_LAST_HANDLE, "");
    EXPECT_EQ(signals1, MX_SOCKET_WRITE_DISABLED | MX_SIGNAL_LAST_HANDLE, "");

    status = mx_socket_write(h0, 0u, "abcde", 5u, &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 5u, "");

    signals1 = get_satisfied_signals(h1);
    EXPECT_EQ(signals1, MX_SOCKET_READABLE | MX_SOCKET_WRITE_DISABLED | MX_SIGNAL_LAST_HANDLE, "");

    status = mx_socket_write(h1, 0u, "fghij", 5u, &count);
    EXPECT_EQ(status, MX_ERR_BAD_STATE, "");

    char rbuf[10] = {0};

    status = mx_socket_read(h0, 0u, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 5u, "");
    EXPECT_EQ(memcmp(rbuf, "12345", 5), 0, "");

    status = mx_socket_read(h0, 0u, rbuf, 1u, &count);
    EXPECT_EQ(status, MX_ERR_BAD_STATE, "");

    signals0 = get_satisfied_signals(h0);
    EXPECT_EQ(signals0, MX_SOCKET_WRITABLE | MX_SOCKET_READ_DISABLED | MX_SIGNAL_LAST_HANDLE, "");

    status = mx_socket_read(h1, 0u, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 5u, "");
    EXPECT_EQ(memcmp(rbuf, "abcde", 5), 0, "");

    mx_handle_close(h0);
    mx_handle_close(h1);

    END_TEST;
}

static bool socket_bytes_outstanding(void) {
    BEGIN_TEST;

    mx_status_t status;
    size_t count;

    mx_handle_t h[2];
    uint32_t read_data[] = { 0, 0 };

    status = mx_socket_create(0, h, h + 1);
    ASSERT_EQ(status, MX_OK, "");

    status = mx_socket_read(h[0], 0u, read_data, sizeof(read_data), &count);
    EXPECT_EQ(status, MX_ERR_SHOULD_WAIT, "");

    static const uint32_t write_data[] = { 0xdeadbeef, 0xc0ffee };
    status = mx_socket_write(h[0], 0u, &write_data[0], sizeof(write_data[0]), &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, sizeof(write_data[0]), "");
    status = mx_socket_write(h[0], 0u, &write_data[1], sizeof(write_data[1]), &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, sizeof(write_data[1]), "");

    // Check the number of bytes outstanding.
    size_t outstanding = 0u;
    status = mx_socket_read(h[1], 0u, NULL, 0, &outstanding);
    EXPECT_EQ(outstanding, sizeof(write_data), "");

    // Check that the prior mx_socket_read call didn't disturb the pending data.
    status = mx_socket_read(h[1], 0u, read_data, sizeof(read_data), &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, sizeof(read_data), "");
    EXPECT_EQ(read_data[0], write_data[0], "");
    EXPECT_EQ(read_data[1], write_data[1], "");

    mx_handle_close(h[1]);

    status = mx_socket_write(h[0], 0u, &write_data[1], sizeof(write_data[1]), &count);
    EXPECT_EQ(status, MX_ERR_PEER_CLOSED, "");

    mx_handle_close(h[0]);

    END_TEST;
}

static bool socket_bytes_outstanding_shutdown_write(void) {
    BEGIN_TEST;

    mx_status_t status;
    size_t count;
    mx_signals_t signals0, signals1;

    mx_handle_t h0, h1;
    status = mx_socket_create(0, &h0, &h1);
    ASSERT_EQ(status, MX_OK, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);

    EXPECT_EQ(signals0, MX_SOCKET_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");
    EXPECT_EQ(signals1, MX_SOCKET_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");

    status = mx_socket_write(h1, 0u, "12345", 5u, &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 5u, "");

    status = mx_socket_write(h1, MX_SOCKET_SHUTDOWN_WRITE, NULL, 0u, NULL);
    EXPECT_EQ(status, MX_OK, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);

    EXPECT_EQ(signals0,
        MX_SOCKET_WRITABLE | MX_SOCKET_READABLE | MX_SIGNAL_LAST_HANDLE,
        "");
    EXPECT_EQ(signals1, MX_SOCKET_WRITE_DISABLED | MX_SIGNAL_LAST_HANDLE, "");

    status = mx_socket_write(h0, 0u, "abcde", 5u, &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 5u, "");

    signals1 = get_satisfied_signals(h1);
    EXPECT_EQ(signals1, MX_SOCKET_READABLE | MX_SOCKET_WRITE_DISABLED | MX_SIGNAL_LAST_HANDLE, "");

    status = mx_socket_write(h1, 0u, "fghij", 5u, &count);
    EXPECT_EQ(status, MX_ERR_BAD_STATE, "");

    char rbuf[10] = {0};

    status = mx_socket_read(h0, 0u, NULL, 0, &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 5u, "");
    count = 0;

    status = mx_socket_read(h0, 0u, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 5u, "");
    EXPECT_EQ(memcmp(rbuf, "12345", 5), 0, "");

    status = mx_socket_read(h0, 0u, rbuf, 1u, &count);
    EXPECT_EQ(status, MX_ERR_BAD_STATE, "");

    signals0 = get_satisfied_signals(h0);
    EXPECT_EQ(signals0, MX_SOCKET_WRITABLE | MX_SOCKET_READ_DISABLED | MX_SIGNAL_LAST_HANDLE, "");

    status = mx_socket_read(h1, 0u, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 5u, "");
    EXPECT_EQ(memcmp(rbuf, "abcde", 5), 0, "");

    mx_handle_close(h0);
    mx_handle_close(h1);

    END_TEST;
}


static bool socket_bytes_outstanding_shutdown_read(void) {
    BEGIN_TEST;

    mx_status_t status;
    size_t count;
    mx_signals_t signals0, signals1;

    mx_handle_t h0, h1;
    status = mx_socket_create(0, &h0, &h1);
    ASSERT_EQ(status, MX_OK, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);

    EXPECT_EQ(signals0, MX_SOCKET_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");
    EXPECT_EQ(signals1, MX_SOCKET_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");

    status = mx_socket_write(h1, 0u, "12345", 5u, &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 5u, "");

    status = mx_socket_write(h0, MX_SOCKET_SHUTDOWN_READ, NULL, 0u, NULL);
    EXPECT_EQ(status, MX_OK, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);

    EXPECT_EQ(signals0, MX_SOCKET_WRITABLE | MX_SOCKET_READABLE | MX_SIGNAL_LAST_HANDLE, "");
    EXPECT_EQ(signals1, MX_SOCKET_WRITE_DISABLED | MX_SIGNAL_LAST_HANDLE, "");

    status = mx_socket_write(h0, 0u, "abcde", 5u, &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 5u, "");

    signals1 = get_satisfied_signals(h1);
    EXPECT_EQ(signals1, MX_SOCKET_READABLE | MX_SOCKET_WRITE_DISABLED | MX_SIGNAL_LAST_HANDLE, "");

    status = mx_socket_write(h1, 0u, "fghij", 5u, &count);
    EXPECT_EQ(status, MX_ERR_BAD_STATE, "");

    char rbuf[10] = {0};

    status = mx_socket_read(h0, 0u, NULL, 0, &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 5u, "");
    count = 0;

    status = mx_socket_read(h0, 0u, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 5u, "");
    EXPECT_EQ(memcmp(rbuf, "12345", 5), 0, "");

    status = mx_socket_read(h0, 0u, rbuf, 1u, &count);
    EXPECT_EQ(status, MX_ERR_BAD_STATE, "");

    signals0 = get_satisfied_signals(h0);
    EXPECT_EQ(signals0, MX_SOCKET_WRITABLE | MX_SOCKET_READ_DISABLED | MX_SIGNAL_LAST_HANDLE, "");

    status = mx_socket_read(h1, 0u, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 5u, "");
    EXPECT_EQ(memcmp(rbuf, "abcde", 5), 0, "");

    mx_handle_close(h0);
    mx_handle_close(h1);

    END_TEST;
}

static bool socket_short_write(void) {
    BEGIN_TEST;

    mx_status_t status;

    mx_handle_t h0, h1;
    status = mx_socket_create(0, &h0, &h1);
    ASSERT_EQ(status, MX_OK, "");

    // TODO(qsr): Request socket buffer and use (socket_buffer + 1).
    const size_t buffer_size = 256 * 1024 + 1;
    char* buffer = malloc(buffer_size);
    size_t written = ~(size_t)0; // This should get overwritten by the syscall.
    status = mx_socket_write(h0, 0u, buffer, buffer_size, &written);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_LT(written, buffer_size, "");

    free(buffer);
    mx_handle_close(h0);
    mx_handle_close(h1);

    END_TEST;
}

static bool socket_datagram(void) {
    BEGIN_TEST;

    size_t count;
    mx_status_t status;
    mx_handle_t h0, h1;
    unsigned char rbuf[4096] = {0}; // bigger than an mbuf

    status = mx_socket_create(MX_SOCKET_DATAGRAM, &h0, &h1);
    ASSERT_EQ(status, MX_OK, "");

    status = mx_socket_write(h0, 0u, "packet1", 8u, &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 8u, "");

    status = mx_socket_write(h0, 0u, "pkt2", 5u, &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 5u, "");

    rbuf[0] = 'a';
    rbuf[1000] = 'b';
    rbuf[2000] = 'c';
    rbuf[3000] = 'd';
    rbuf[4000] = 'e';
    rbuf[4095] = 'f';
    status = mx_socket_write(h0, 0u, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, sizeof(rbuf), "");

    status = mx_socket_read(h1, 0u, NULL, 0, &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, sizeof(rbuf) + 8u + 5u, "");
    count = 0;

    bzero(rbuf, sizeof(rbuf));
    status = mx_socket_read(h1, 0u, rbuf, 3, &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 3u, "");
    EXPECT_EQ(memcmp(rbuf, "pac", 4), 0, ""); // short read "packet1"
    count = 0;

    status = mx_socket_read(h1, 0u, NULL, 0, &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, sizeof(rbuf) + 5u, "");
    count = 0;

    status = mx_socket_read(h1, 0u, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 5u, "");
    EXPECT_EQ(memcmp(rbuf, "pkt2", 5), 0, "");

    status = mx_socket_read(h1, 0u, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, sizeof(rbuf), "");
    EXPECT_EQ(rbuf[0], 'a', "");
    EXPECT_EQ(rbuf[1000], 'b', "");
    EXPECT_EQ(rbuf[2000], 'c', "");
    EXPECT_EQ(rbuf[3000], 'd', "");
    EXPECT_EQ(rbuf[4000], 'e', "");
    EXPECT_EQ(rbuf[4095], 'f', "");

    status = mx_socket_read(h1, 0u, NULL, 0, &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 0u, "");

    END_TEST;
}

static bool socket_datagram_no_short_write(void) {
    BEGIN_TEST;

    mx_status_t status;

    mx_handle_t h0, h1;
    status = mx_socket_create(MX_SOCKET_DATAGRAM, &h0, &h1);
    ASSERT_EQ(status, MX_OK, "");

    // TODO(qsr): Request socket buffer and use (socket_buffer + 1).
    const size_t buffer_size = 256 * 1024 + 1;
    char* buffer = malloc(buffer_size);
    size_t written = 999;
    status = mx_socket_write(h0, 0u, buffer, buffer_size, &written);
    EXPECT_EQ(status, MX_ERR_SHOULD_WAIT, "");
    // Since the syscall failed, it should not have overwritten this output
    // parameter.
    EXPECT_EQ(written, 999u, "");

    free(buffer);
    mx_handle_close(h0);
    mx_handle_close(h1);

    END_TEST;
}

static bool socket_control_plane_absent(void) {
    BEGIN_TEST;

    mx_status_t status;

    mx_handle_t h0, h1;
    status = mx_socket_create(0, &h0, &h1);
    ASSERT_EQ(status, MX_OK, "");

    status = mx_socket_write(h0, MX_SOCKET_CONTROL, "hi", 2u, NULL);
    EXPECT_EQ(status, MX_ERR_BAD_STATE, "");

    status = mx_socket_write(h1, MX_SOCKET_CONTROL, "hi", 2u, NULL);
    EXPECT_EQ(status, MX_ERR_BAD_STATE, "");

    size_t count;
    char rbuf[10] = {0};

    status = mx_socket_read(h0, MX_SOCKET_CONTROL, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, MX_ERR_BAD_STATE, "");

    status = mx_socket_read(h1, MX_SOCKET_CONTROL, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, MX_ERR_BAD_STATE, "");

    END_TEST;
}

static bool socket_control_plane(void) {
    BEGIN_TEST;

    mx_status_t status;

    mx_handle_t h0, h1;
    status = mx_socket_create(MX_SOCKET_HAS_CONTROL, &h0, &h1);
    ASSERT_EQ(status, MX_OK, "");

    mx_signals_t signals0 = get_satisfied_signals(h0);
    mx_signals_t signals1 = get_satisfied_signals(h1);
    EXPECT_EQ(signals0, MX_SOCKET_WRITABLE | MX_SOCKET_CONTROL_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");
    EXPECT_EQ(signals1, MX_SOCKET_WRITABLE | MX_SOCKET_CONTROL_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");

    // Write to the control plane.
    size_t count;
    status = mx_socket_write(h0, MX_SOCKET_CONTROL, "hello1", 6u, &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 6u, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);
    EXPECT_EQ(signals0, MX_SOCKET_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");
    EXPECT_EQ(signals1, MX_SOCKET_WRITABLE | MX_SOCKET_CONTROL_READABLE | MX_SOCKET_CONTROL_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");

    status = mx_socket_write(h0, MX_SOCKET_CONTROL, "hi", 2u, NULL);
    EXPECT_EQ(status, MX_ERR_SHOULD_WAIT, "");

    status = mx_socket_write(h1, MX_SOCKET_CONTROL, "hello0", 6u, &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 6u, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);
    EXPECT_EQ(signals0, MX_SOCKET_WRITABLE | MX_SOCKET_CONTROL_READABLE | MX_SIGNAL_LAST_HANDLE, "");
    EXPECT_EQ(signals1, MX_SOCKET_WRITABLE | MX_SOCKET_CONTROL_READABLE | MX_SIGNAL_LAST_HANDLE, "");

    status = mx_socket_write(h1, MX_SOCKET_CONTROL, "hi", 2u, NULL);
    EXPECT_EQ(status, MX_ERR_SHOULD_WAIT, "");

    char rbuf[10] = {0};

    // The control plane is independent of normal reads and writes.
    status = mx_socket_read(h0, 0, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, MX_ERR_SHOULD_WAIT, "");
    status = mx_socket_read(h1, 0, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, MX_ERR_SHOULD_WAIT, "");
    status = mx_socket_write(h0, 0, "normal", 7u, &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 7u, "");
    status = mx_socket_read(h1, 0, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 7u, "");
    EXPECT_EQ(memcmp(rbuf, "normal", 7), 0, "");

    // Read from the control plane.
    status = mx_socket_read(h0, MX_SOCKET_CONTROL, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 6u, "");
    EXPECT_EQ(memcmp(rbuf, "hello0", 6), 0, "");

    status = mx_socket_read(h0, MX_SOCKET_CONTROL, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, MX_ERR_SHOULD_WAIT, "");

    status = mx_socket_read(h1, MX_SOCKET_CONTROL, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 6u, "");
    EXPECT_EQ(memcmp(rbuf, "hello1", 6), 0, "");

    status = mx_socket_read(h1, MX_SOCKET_CONTROL, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, MX_ERR_SHOULD_WAIT, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);
    EXPECT_EQ(signals0, MX_SOCKET_WRITABLE | MX_SOCKET_CONTROL_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");
    EXPECT_EQ(signals1, MX_SOCKET_WRITABLE | MX_SOCKET_CONTROL_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");

    END_TEST;
}

static bool socket_control_plane_shutdown(void) {
    BEGIN_TEST;

    mx_status_t status;
    size_t count;

    mx_handle_t h0, h1;
    status = mx_socket_create(MX_SOCKET_HAS_CONTROL, &h0, &h1);
    ASSERT_EQ(status, MX_OK, "");

    mx_signals_t signals0 = get_satisfied_signals(h0);
    mx_signals_t signals1 = get_satisfied_signals(h1);
    EXPECT_EQ(signals0, MX_SOCKET_WRITABLE | MX_SOCKET_CONTROL_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");
    EXPECT_EQ(signals1, MX_SOCKET_WRITABLE | MX_SOCKET_CONTROL_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");

    status = mx_socket_write(h1, 0u, "12345", 5u, &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 5u, "");

    status = mx_socket_write(h1, MX_SOCKET_SHUTDOWN_WRITE, NULL, 0u, NULL);
    EXPECT_EQ(status, MX_OK, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);
    EXPECT_EQ(signals0, MX_SOCKET_WRITABLE | MX_SOCKET_CONTROL_WRITABLE | MX_SOCKET_READABLE | MX_SIGNAL_LAST_HANDLE, "");
    EXPECT_EQ(signals1, MX_SOCKET_WRITE_DISABLED | MX_SOCKET_CONTROL_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");

    status = mx_socket_write(h0, MX_SOCKET_CONTROL, "hello1", 6u, &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 6u, "");

    status = mx_socket_write(h1, MX_SOCKET_CONTROL, "hello0", 6u, &count);
    EXPECT_EQ(status, MX_OK, "");
    EXPECT_EQ(count, 6u, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);
    EXPECT_EQ(signals0, MX_SOCKET_WRITABLE | MX_SOCKET_CONTROL_READABLE | MX_SOCKET_READABLE | MX_SIGNAL_LAST_HANDLE, "");
    EXPECT_EQ(signals1, MX_SOCKET_WRITE_DISABLED | MX_SOCKET_CONTROL_READABLE | MX_SIGNAL_LAST_HANDLE, "");

    END_TEST;
}


BEGIN_TEST_CASE(socket_tests)
RUN_TEST(socket_basic)
RUN_TEST(socket_signals)
RUN_TEST(socket_shutdown_write)
RUN_TEST(socket_shutdown_read)
RUN_TEST(socket_bytes_outstanding)
RUN_TEST(socket_bytes_outstanding_shutdown_write)
RUN_TEST(socket_bytes_outstanding_shutdown_read)
RUN_TEST(socket_short_write)
RUN_TEST(socket_datagram)
RUN_TEST(socket_datagram_no_short_write)
RUN_TEST(socket_control_plane_absent)
RUN_TEST(socket_control_plane)
RUN_TEST(socket_control_plane_shutdown)
END_TEST_CASE(socket_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
