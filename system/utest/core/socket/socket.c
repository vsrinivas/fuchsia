// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <zircon/syscalls.h>
#include <unittest/unittest.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static zx_signals_t get_satisfied_signals(zx_handle_t handle) {
    zx_signals_t pending = 0;
    zx_object_wait_one(handle, 0u, 0u, &pending);
    return pending;
}

static bool socket_basic(void) {
    BEGIN_TEST;

    zx_status_t status;
    size_t count;

    zx_handle_t h[2];
    uint32_t read_data[] = { 0, 0 };

    status = zx_socket_create(0, h, h + 1);
    ASSERT_EQ(status, ZX_OK, "");

    // Check that koids line up.
    zx_info_handle_basic_t info[2] = {};
    status = zx_object_get_info(h[0], ZX_INFO_HANDLE_BASIC, &info[0], sizeof(info[0]), NULL, NULL);
    ASSERT_EQ(status, ZX_OK, "");
    status = zx_object_get_info(h[1], ZX_INFO_HANDLE_BASIC, &info[1], sizeof(info[1]), NULL, NULL);
    ASSERT_EQ(status, ZX_OK, "");
    ASSERT_NE(info[0].koid, 0u, "zero koid!");
    ASSERT_NE(info[0].related_koid, 0u, "zero peer koid!");
    ASSERT_NE(info[1].koid, 0u, "zero koid!");
    ASSERT_NE(info[1].related_koid, 0u, "zero peer koid!");
    ASSERT_EQ(info[0].koid, info[1].related_koid, "mismatched koids!");
    ASSERT_EQ(info[1].koid, info[0].related_koid, "mismatched koids!");

    status = zx_socket_read(h[0], 0u, read_data, sizeof(read_data), &count);
    EXPECT_EQ(status, ZX_ERR_SHOULD_WAIT, "");

    static const uint32_t write_data[] = { 0xdeadbeef, 0xc0ffee };
    status = zx_socket_write(h[0], 0u, &write_data[0], sizeof(write_data[0]), &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, sizeof(write_data[0]), "");
    status = zx_socket_write(h[0], 0u, &write_data[1], sizeof(write_data[1]), &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, sizeof(write_data[1]), "");

    status = zx_socket_read(h[1], 0u, read_data, sizeof(read_data), &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, sizeof(read_data), "");
    EXPECT_EQ(read_data[0], write_data[0], "");
    EXPECT_EQ(read_data[1], write_data[1], "");

    status = zx_socket_write(h[0], 0u, write_data, sizeof(write_data), NULL);
    EXPECT_EQ(status, ZX_OK, "");
    memset(read_data, 0, sizeof(read_data));
    status = zx_socket_read(h[1], 0u, read_data, sizeof(read_data), NULL);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(read_data[0], write_data[0], "");
    EXPECT_EQ(read_data[1], write_data[1], "");

    zx_handle_close(h[1]);

    status = zx_socket_write(h[0], 0u, &write_data[1], sizeof(write_data[1]), &count);
    EXPECT_EQ(status, ZX_ERR_PEER_CLOSED, "");

    zx_handle_close(h[0]);
    END_TEST;
}

static bool socket_signals(void) {
    BEGIN_TEST;

    zx_status_t status;
    size_t count;

    zx_handle_t h0, h1;
    status = zx_socket_create(0, &h0, &h1);
    ASSERT_EQ(status, ZX_OK, "");

    zx_signals_t signals0 = get_satisfied_signals(h0);
    zx_signals_t signals1 = get_satisfied_signals(h1);

    EXPECT_EQ(signals0, ZX_SOCKET_WRITABLE, "");
    EXPECT_EQ(signals1, ZX_SOCKET_WRITABLE, "");

    const size_t kAllSize = 128 * 1024;
    char* big_buf =  (char*) malloc(kAllSize);
    ASSERT_NONNULL(big_buf, "");

    memset(big_buf, 0x66, kAllSize);

    status = zx_socket_write(h0, 0u, big_buf, kAllSize / 16, &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, kAllSize / 16, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);

    EXPECT_EQ(signals0, ZX_SOCKET_WRITABLE, "");
    EXPECT_EQ(signals1, ZX_SOCKET_READABLE | ZX_SOCKET_WRITABLE, "");

    status = zx_socket_read(h1, 0u, big_buf, kAllSize, &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, kAllSize / 16, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);

    EXPECT_EQ(signals0, ZX_SOCKET_WRITABLE, "");
    EXPECT_EQ(signals1, ZX_SOCKET_WRITABLE, "");

    status = zx_object_signal_peer(h0, ZX_SOCKET_WRITABLE, 0u);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS, "");

    status = zx_object_signal_peer(h0, 0u, ZX_USER_SIGNAL_1);
    EXPECT_EQ(status, ZX_OK, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);

    EXPECT_EQ(signals0, ZX_SOCKET_WRITABLE, "");
    EXPECT_EQ(signals1, ZX_SOCKET_WRITABLE | ZX_USER_SIGNAL_1, "");

    zx_handle_close(h1);

    signals0 = get_satisfied_signals(h0);
    EXPECT_EQ(signals0, ZX_SOCKET_PEER_CLOSED, "");

    zx_handle_close(h0);

    free(big_buf);
    END_TEST;
}

static bool socket_peer_closed(void) {
    BEGIN_TEST;

    zx_handle_t socket[2];
    ASSERT_EQ(zx_socket_create(0, &socket[0], &socket[1]), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(socket[1]), ZX_OK, "");
    ASSERT_EQ(zx_object_signal_peer(socket[0], 0u, ZX_USER_SIGNAL_0), ZX_ERR_PEER_CLOSED, "");
    ASSERT_EQ(zx_handle_close(socket[0]), ZX_OK, "");

    END_TEST;
}

static bool socket_shutdown_write(void) {
    BEGIN_TEST;

    zx_status_t status;
    size_t count;
    zx_signals_t signals0, signals1;

    zx_handle_t h0, h1;
    status = zx_socket_create(0, &h0, &h1);
    ASSERT_EQ(status, ZX_OK, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);

    EXPECT_EQ(signals0, ZX_SOCKET_WRITABLE, "");
    EXPECT_EQ(signals1, ZX_SOCKET_WRITABLE, "");

    status = zx_socket_write(h1, 0u, "12345", 5u, &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 5u, "");

    status = zx_socket_write(h1, ZX_SOCKET_SHUTDOWN_WRITE, NULL, 0u, NULL);
    EXPECT_EQ(status, ZX_OK, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);

    EXPECT_EQ(signals0,
        ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE,
        "");
    EXPECT_EQ(signals1, ZX_SOCKET_WRITE_DISABLED, "");

    status = zx_socket_write(h0, 0u, "abcde", 5u, &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 5u, "");

    signals1 = get_satisfied_signals(h1);
    EXPECT_EQ(signals1, ZX_SOCKET_READABLE | ZX_SOCKET_WRITE_DISABLED, "");

    status = zx_socket_write(h1, 0u, "fghij", 5u, &count);
    EXPECT_EQ(status, ZX_ERR_BAD_STATE, "");

    char rbuf[10] = {0};

    status = zx_socket_read(h0, 0u, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 5u, "");
    EXPECT_EQ(memcmp(rbuf, "12345", 5), 0, "");

    status = zx_socket_read(h0, 0u, rbuf, 1u, &count);
    EXPECT_EQ(status, ZX_ERR_BAD_STATE, "");

    signals0 = get_satisfied_signals(h0);
    EXPECT_EQ(signals0, ZX_SOCKET_WRITABLE | ZX_SOCKET_READ_DISABLED, "");

    status = zx_socket_read(h1, 0u, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 5u, "");
    EXPECT_EQ(memcmp(rbuf, "abcde", 5), 0, "");

    zx_handle_close(h0);

    // Calling shutdown after the peer is closed is completely valid.
    status = zx_socket_write(h1, ZX_SOCKET_SHUTDOWN_READ, NULL, 0u, NULL);
    EXPECT_EQ(status, ZX_OK, "");

    signals1 = get_satisfied_signals(h1);
    EXPECT_EQ(signals1, ZX_SOCKET_READ_DISABLED | ZX_SOCKET_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED, "");

    zx_handle_close(h1);

    END_TEST;
}

static bool socket_shutdown_read(void) {
    BEGIN_TEST;

    zx_status_t status;
    size_t count;
    zx_signals_t signals0, signals1;

    zx_handle_t h0, h1;
    status = zx_socket_create(0, &h0, &h1);
    ASSERT_EQ(status, ZX_OK, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);

    EXPECT_EQ(signals0, ZX_SOCKET_WRITABLE, "");
    EXPECT_EQ(signals1, ZX_SOCKET_WRITABLE, "");

    status = zx_socket_write(h1, 0u, "12345", 5u, &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 5u, "");

    status = zx_socket_write(h0, ZX_SOCKET_SHUTDOWN_READ, NULL, 0u, NULL);
    EXPECT_EQ(status, ZX_OK, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);

    EXPECT_EQ(signals0, ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE, "");
    EXPECT_EQ(signals1, ZX_SOCKET_WRITE_DISABLED, "");

    status = zx_socket_write(h0, 0u, "abcde", 5u, &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 5u, "");

    signals1 = get_satisfied_signals(h1);
    EXPECT_EQ(signals1, ZX_SOCKET_READABLE | ZX_SOCKET_WRITE_DISABLED, "");

    status = zx_socket_write(h1, 0u, "fghij", 5u, &count);
    EXPECT_EQ(status, ZX_ERR_BAD_STATE, "");

    char rbuf[10] = {0};

    status = zx_socket_read(h0, 0u, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 5u, "");
    EXPECT_EQ(memcmp(rbuf, "12345", 5), 0, "");

    status = zx_socket_read(h0, 0u, rbuf, 1u, &count);
    EXPECT_EQ(status, ZX_ERR_BAD_STATE, "");

    signals0 = get_satisfied_signals(h0);
    EXPECT_EQ(signals0, ZX_SOCKET_WRITABLE | ZX_SOCKET_READ_DISABLED, "");

    status = zx_socket_read(h1, 0u, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 5u, "");
    EXPECT_EQ(memcmp(rbuf, "abcde", 5), 0, "");

    zx_handle_close(h0);
    zx_handle_close(h1);

    END_TEST;
}

static bool socket_bytes_outstanding(void) {
    BEGIN_TEST;

    zx_status_t status;
    size_t count;

    zx_handle_t h[2];
    uint32_t read_data[] = { 0, 0 };

    status = zx_socket_create(0, h, h + 1);
    ASSERT_EQ(status, ZX_OK, "");

    status = zx_socket_read(h[0], 0u, read_data, sizeof(read_data), &count);
    EXPECT_EQ(status, ZX_ERR_SHOULD_WAIT, "");

    static const uint32_t write_data[] = { 0xdeadbeef, 0xc0ffee };
    status = zx_socket_write(h[0], 0u, &write_data[0], sizeof(write_data[0]), &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, sizeof(write_data[0]), "");
    status = zx_socket_write(h[0], 0u, &write_data[1], sizeof(write_data[1]), &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, sizeof(write_data[1]), "");

    // Check the number of bytes outstanding.
    size_t outstanding = 0u;
    status = zx_socket_read(h[1], 0u, NULL, 0, &outstanding);
    EXPECT_EQ(outstanding, sizeof(write_data), "");

    // Check that the prior zx_socket_read call didn't disturb the pending data.
    status = zx_socket_read(h[1], 0u, read_data, sizeof(read_data), &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, sizeof(read_data), "");
    EXPECT_EQ(read_data[0], write_data[0], "");
    EXPECT_EQ(read_data[1], write_data[1], "");

    zx_handle_close(h[1]);

    status = zx_socket_write(h[0], 0u, &write_data[1], sizeof(write_data[1]), &count);
    EXPECT_EQ(status, ZX_ERR_PEER_CLOSED, "");

    zx_handle_close(h[0]);

    END_TEST;
}

static bool socket_bytes_outstanding_shutdown_write(void) {
    BEGIN_TEST;

    zx_status_t status;
    size_t count;
    zx_signals_t signals0, signals1;

    zx_handle_t h0, h1;
    status = zx_socket_create(0, &h0, &h1);
    ASSERT_EQ(status, ZX_OK, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);

    EXPECT_EQ(signals0, ZX_SOCKET_WRITABLE, "");
    EXPECT_EQ(signals1, ZX_SOCKET_WRITABLE, "");

    status = zx_socket_write(h1, 0u, "12345", 5u, &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 5u, "");

    status = zx_socket_write(h1, ZX_SOCKET_SHUTDOWN_WRITE, NULL, 0u, NULL);
    EXPECT_EQ(status, ZX_OK, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);

    EXPECT_EQ(signals0,
        ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE,
        "");
    EXPECT_EQ(signals1, ZX_SOCKET_WRITE_DISABLED, "");

    status = zx_socket_write(h0, 0u, "abcde", 5u, &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 5u, "");

    signals1 = get_satisfied_signals(h1);
    EXPECT_EQ(signals1, ZX_SOCKET_READABLE | ZX_SOCKET_WRITE_DISABLED, "");

    status = zx_socket_write(h1, 0u, "fghij", 5u, &count);
    EXPECT_EQ(status, ZX_ERR_BAD_STATE, "");

    char rbuf[10] = {0};

    status = zx_socket_read(h0, 0u, NULL, 0, &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 5u, "");
    count = 0;

    status = zx_socket_read(h0, 0u, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 5u, "");
    EXPECT_EQ(memcmp(rbuf, "12345", 5), 0, "");

    status = zx_socket_read(h0, 0u, rbuf, 1u, &count);
    EXPECT_EQ(status, ZX_ERR_BAD_STATE, "");

    signals0 = get_satisfied_signals(h0);
    EXPECT_EQ(signals0, ZX_SOCKET_WRITABLE | ZX_SOCKET_READ_DISABLED, "");

    status = zx_socket_read(h1, 0u, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 5u, "");
    EXPECT_EQ(memcmp(rbuf, "abcde", 5), 0, "");

    zx_handle_close(h0);
    zx_handle_close(h1);

    END_TEST;
}


static bool socket_bytes_outstanding_shutdown_read(void) {
    BEGIN_TEST;

    zx_status_t status;
    size_t count;
    zx_signals_t signals0, signals1;

    zx_handle_t h0, h1;
    status = zx_socket_create(0, &h0, &h1);
    ASSERT_EQ(status, ZX_OK, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);

    EXPECT_EQ(signals0, ZX_SOCKET_WRITABLE, "");
    EXPECT_EQ(signals1, ZX_SOCKET_WRITABLE, "");

    status = zx_socket_write(h1, 0u, "12345", 5u, &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 5u, "");

    status = zx_socket_write(h0, ZX_SOCKET_SHUTDOWN_READ, NULL, 0u, NULL);
    EXPECT_EQ(status, ZX_OK, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);

    EXPECT_EQ(signals0, ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE, "");
    EXPECT_EQ(signals1, ZX_SOCKET_WRITE_DISABLED, "");

    status = zx_socket_write(h0, 0u, "abcde", 5u, &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 5u, "");

    signals1 = get_satisfied_signals(h1);
    EXPECT_EQ(signals1, ZX_SOCKET_READABLE | ZX_SOCKET_WRITE_DISABLED, "");

    status = zx_socket_write(h1, 0u, "fghij", 5u, &count);
    EXPECT_EQ(status, ZX_ERR_BAD_STATE, "");

    char rbuf[10] = {0};

    status = zx_socket_read(h0, 0u, NULL, 0, &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 5u, "");
    count = 0;

    status = zx_socket_read(h0, 0u, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 5u, "");
    EXPECT_EQ(memcmp(rbuf, "12345", 5), 0, "");

    status = zx_socket_read(h0, 0u, rbuf, 1u, &count);
    EXPECT_EQ(status, ZX_ERR_BAD_STATE, "");

    signals0 = get_satisfied_signals(h0);
    EXPECT_EQ(signals0, ZX_SOCKET_WRITABLE | ZX_SOCKET_READ_DISABLED, "");

    status = zx_socket_read(h1, 0u, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 5u, "");
    EXPECT_EQ(memcmp(rbuf, "abcde", 5), 0, "");

    zx_handle_close(h0);
    zx_handle_close(h1);

    END_TEST;
}

static bool socket_short_write(void) {
    BEGIN_TEST;

    zx_status_t status;

    zx_handle_t h0, h1;
    status = zx_socket_create(0, &h0, &h1);
    ASSERT_EQ(status, ZX_OK, "");

    // TODO(qsr): Request socket buffer and use (socket_buffer + 1).
    const size_t buffer_size = 256 * 1024 + 1;
    char* buffer = malloc(buffer_size);
    size_t written = ~(size_t)0; // This should get overwritten by the syscall.
    status = zx_socket_write(h0, 0u, buffer, buffer_size, &written);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_LT(written, buffer_size, "");

    free(buffer);
    zx_handle_close(h0);
    zx_handle_close(h1);

    END_TEST;
}

static bool socket_datagram(void) {
    BEGIN_TEST;

    size_t count;
    zx_status_t status;
    zx_handle_t h0, h1;
    unsigned char rbuf[4096] = {0}; // bigger than an mbuf

    status = zx_socket_create(ZX_SOCKET_DATAGRAM, &h0, &h1);
    ASSERT_EQ(status, ZX_OK, "");

    status = zx_socket_write(h0, 0u, "packet1", 8u, &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 8u, "");

    status = zx_socket_write(h0, 0u, "pkt2", 5u, &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 5u, "");

    rbuf[0] = 'a';
    rbuf[1000] = 'b';
    rbuf[2000] = 'c';
    rbuf[3000] = 'd';
    rbuf[4000] = 'e';
    rbuf[4095] = 'f';
    status = zx_socket_write(h0, 0u, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, sizeof(rbuf), "");

    status = zx_socket_read(h1, 0u, NULL, 0, &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, sizeof(rbuf) + 8u + 5u, "");
    count = 0;

    bzero(rbuf, sizeof(rbuf));
    status = zx_socket_read(h1, 0u, rbuf, 3, &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 3u, "");
    EXPECT_EQ(memcmp(rbuf, "pac", 4), 0, ""); // short read "packet1"
    count = 0;

    status = zx_socket_read(h1, 0u, NULL, 0, &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, sizeof(rbuf) + 5u, "");
    count = 0;

    status = zx_socket_read(h1, 0u, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 5u, "");
    EXPECT_EQ(memcmp(rbuf, "pkt2", 5), 0, "");

    status = zx_socket_read(h1, 0u, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, sizeof(rbuf), "");
    EXPECT_EQ(rbuf[0], 'a', "");
    EXPECT_EQ(rbuf[1000], 'b', "");
    EXPECT_EQ(rbuf[2000], 'c', "");
    EXPECT_EQ(rbuf[3000], 'd', "");
    EXPECT_EQ(rbuf[4000], 'e', "");
    EXPECT_EQ(rbuf[4095], 'f', "");

    status = zx_socket_read(h1, 0u, NULL, 0, &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 0u, "");

    END_TEST;
}

static bool socket_datagram_no_short_write(void) {
    BEGIN_TEST;

    zx_status_t status;

    zx_handle_t h0, h1;
    status = zx_socket_create(ZX_SOCKET_DATAGRAM, &h0, &h1);
    ASSERT_EQ(status, ZX_OK, "");

    // TODO(qsr): Request socket buffer and use (socket_buffer + 1).
    const size_t buffer_size = 256 * 1024 + 1;
    char* buffer = malloc(buffer_size);
    size_t written = 999;
    status = zx_socket_write(h0, 0u, buffer, buffer_size, &written);
    EXPECT_EQ(status, ZX_ERR_SHOULD_WAIT, "");
    // Since the syscall failed, it should not have overwritten this output
    // parameter.
    EXPECT_EQ(written, 999u, "");

    free(buffer);
    zx_handle_close(h0);
    zx_handle_close(h1);

    END_TEST;
}

static bool socket_control_plane_absent(void) {
    BEGIN_TEST;

    zx_status_t status;

    zx_handle_t h0, h1;
    status = zx_socket_create(0, &h0, &h1);
    ASSERT_EQ(status, ZX_OK, "");

    status = zx_socket_write(h0, ZX_SOCKET_CONTROL, "hi", 2u, NULL);
    EXPECT_EQ(status, ZX_ERR_BAD_STATE, "");

    status = zx_socket_write(h1, ZX_SOCKET_CONTROL, "hi", 2u, NULL);
    EXPECT_EQ(status, ZX_ERR_BAD_STATE, "");

    size_t count;
    char rbuf[10] = {0};

    status = zx_socket_read(h0, ZX_SOCKET_CONTROL, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, ZX_ERR_BAD_STATE, "");

    status = zx_socket_read(h1, ZX_SOCKET_CONTROL, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, ZX_ERR_BAD_STATE, "");

    END_TEST;
}

static bool socket_control_plane(void) {
    BEGIN_TEST;

    zx_status_t status;

    zx_handle_t h0, h1;
    status = zx_socket_create(ZX_SOCKET_HAS_CONTROL, &h0, &h1);
    ASSERT_EQ(status, ZX_OK, "");

    zx_signals_t signals0 = get_satisfied_signals(h0);
    zx_signals_t signals1 = get_satisfied_signals(h1);
    EXPECT_EQ(signals0, ZX_SOCKET_WRITABLE | ZX_SOCKET_CONTROL_WRITABLE, "");
    EXPECT_EQ(signals1, ZX_SOCKET_WRITABLE | ZX_SOCKET_CONTROL_WRITABLE, "");

    // Write to the control plane.
    size_t count;
    status = zx_socket_write(h0, ZX_SOCKET_CONTROL, "hello1", 6u, &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 6u, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);
    EXPECT_EQ(signals0, ZX_SOCKET_WRITABLE, "");
    EXPECT_EQ(signals1, ZX_SOCKET_WRITABLE | ZX_SOCKET_CONTROL_READABLE | ZX_SOCKET_CONTROL_WRITABLE, "");

    status = zx_socket_write(h0, ZX_SOCKET_CONTROL, "hi", 2u, NULL);
    EXPECT_EQ(status, ZX_ERR_SHOULD_WAIT, "");

    status = zx_socket_write(h1, ZX_SOCKET_CONTROL, "hello0", 6u, &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 6u, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);
    EXPECT_EQ(signals0, ZX_SOCKET_WRITABLE | ZX_SOCKET_CONTROL_READABLE, "");
    EXPECT_EQ(signals1, ZX_SOCKET_WRITABLE | ZX_SOCKET_CONTROL_READABLE, "");

    status = zx_socket_write(h1, ZX_SOCKET_CONTROL, "hi", 2u, NULL);
    EXPECT_EQ(status, ZX_ERR_SHOULD_WAIT, "");

    char rbuf[10] = {0};

    // The control plane is independent of normal reads and writes.
    status = zx_socket_read(h0, 0, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, ZX_ERR_SHOULD_WAIT, "");
    status = zx_socket_read(h1, 0, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, ZX_ERR_SHOULD_WAIT, "");
    status = zx_socket_write(h0, 0, "normal", 7u, &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 7u, "");
    status = zx_socket_read(h1, 0, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 7u, "");
    EXPECT_EQ(memcmp(rbuf, "normal", 7), 0, "");

    // Read from the control plane.
    status = zx_socket_read(h0, ZX_SOCKET_CONTROL, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 6u, "");
    EXPECT_EQ(memcmp(rbuf, "hello0", 6), 0, "");

    status = zx_socket_read(h0, ZX_SOCKET_CONTROL, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, ZX_ERR_SHOULD_WAIT, "");

    status = zx_socket_read(h1, ZX_SOCKET_CONTROL, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 6u, "");
    EXPECT_EQ(memcmp(rbuf, "hello1", 6), 0, "");

    status = zx_socket_read(h1, ZX_SOCKET_CONTROL, rbuf, sizeof(rbuf), &count);
    EXPECT_EQ(status, ZX_ERR_SHOULD_WAIT, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);
    EXPECT_EQ(signals0, ZX_SOCKET_WRITABLE | ZX_SOCKET_CONTROL_WRITABLE, "");
    EXPECT_EQ(signals1, ZX_SOCKET_WRITABLE | ZX_SOCKET_CONTROL_WRITABLE, "");

    END_TEST;
}

static bool socket_control_plane_shutdown(void) {
    BEGIN_TEST;

    zx_status_t status;
    size_t count;

    zx_handle_t h0, h1;
    status = zx_socket_create(ZX_SOCKET_HAS_CONTROL, &h0, &h1);
    ASSERT_EQ(status, ZX_OK, "");

    zx_signals_t signals0 = get_satisfied_signals(h0);
    zx_signals_t signals1 = get_satisfied_signals(h1);
    EXPECT_EQ(signals0, ZX_SOCKET_WRITABLE | ZX_SOCKET_CONTROL_WRITABLE, "");
    EXPECT_EQ(signals1, ZX_SOCKET_WRITABLE | ZX_SOCKET_CONTROL_WRITABLE, "");

    status = zx_socket_write(h1, 0u, "12345", 5u, &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 5u, "");

    status = zx_socket_write(h1, ZX_SOCKET_SHUTDOWN_WRITE, NULL, 0u, NULL);
    EXPECT_EQ(status, ZX_OK, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);
    EXPECT_EQ(signals0, ZX_SOCKET_WRITABLE | ZX_SOCKET_CONTROL_WRITABLE | ZX_SOCKET_READABLE, "");
    EXPECT_EQ(signals1, ZX_SOCKET_WRITE_DISABLED | ZX_SOCKET_CONTROL_WRITABLE, "");

    status = zx_socket_write(h0, ZX_SOCKET_CONTROL, "hello1", 6u, &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 6u, "");

    status = zx_socket_write(h1, ZX_SOCKET_CONTROL, "hello0", 6u, &count);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_EQ(count, 6u, "");

    signals0 = get_satisfied_signals(h0);
    signals1 = get_satisfied_signals(h1);
    EXPECT_EQ(signals0, ZX_SOCKET_WRITABLE | ZX_SOCKET_CONTROL_READABLE | ZX_SOCKET_READABLE, "");
    EXPECT_EQ(signals1, ZX_SOCKET_WRITE_DISABLED | ZX_SOCKET_CONTROL_READABLE, "");

    END_TEST;
}

static bool socket_accept(void) {
    BEGIN_TEST;

    zx_status_t status;

    zx_handle_t a0, a1;
    status = zx_socket_create(ZX_SOCKET_HAS_ACCEPT, &a0, &a1);
    ASSERT_EQ(status, ZX_OK, "");

    zx_handle_t b0, b1;
    status = zx_socket_create(0, &b0, &b1);
    ASSERT_EQ(status, ZX_OK, "");

    zx_handle_t c0, c1;
    status = zx_socket_create(0, &c0, &c1);
    ASSERT_EQ(status, ZX_OK, "");

    zx_signals_t signals0 = get_satisfied_signals(a0);
    zx_signals_t signals1 = get_satisfied_signals(a1);
    EXPECT_EQ(signals0, ZX_SOCKET_WRITABLE | ZX_SOCKET_SHARE, "");
    EXPECT_EQ(signals1, ZX_SOCKET_WRITABLE | ZX_SOCKET_SHARE, "");

    // cannot share a HAS_ACCEPT socket
    status = zx_socket_share(b0, a0);
    EXPECT_EQ(status, ZX_ERR_BAD_STATE, "");

    // cannot share via a non-HAS_ACCEPT socket
    status = zx_socket_share(b0, c0);
    EXPECT_EQ(status, ZX_ERR_NOT_SUPPORTED, "");

    // cannot share a socket via itself (either direction)
    status = zx_socket_share(a0, a0);
    EXPECT_EQ(status, ZX_ERR_BAD_STATE, "");
    status = zx_socket_share(a0, a1);
    EXPECT_EQ(status, ZX_ERR_BAD_STATE, "");
    status = zx_socket_share(a1, a0);
    EXPECT_EQ(status, ZX_ERR_BAD_STATE, "");
    status = zx_socket_share(a1, a1);
    EXPECT_EQ(status, ZX_ERR_BAD_STATE, "");

    // cannot accept from a non-HAS_ACCEPT socket
    zx_handle_t h;
    status = zx_socket_accept(b0, &h);
    EXPECT_EQ(status, ZX_ERR_NOT_SUPPORTED, "");

    status = zx_socket_share(a0, b0);
    EXPECT_EQ(status, ZX_OK, "");

    signals0 = get_satisfied_signals(a0);
    signals1 = get_satisfied_signals(a1);
    EXPECT_EQ(signals0, ZX_SOCKET_WRITABLE, "");
    EXPECT_EQ(signals1, ZX_SOCKET_WRITABLE | ZX_SOCKET_SHARE | ZX_SOCKET_ACCEPT, "");

    // queue is only one deep
    status = zx_socket_share(a0, b1);
    EXPECT_EQ(status, ZX_ERR_SHOULD_WAIT, "");

    status = zx_socket_accept(a1, &h);
    EXPECT_EQ(status, ZX_OK, "");
    b0 = h;

    // queue is only one deep
    status = zx_socket_accept(a0, &h);
    EXPECT_EQ(status, ZX_ERR_SHOULD_WAIT, "");

    signals0 = get_satisfied_signals(a0);
    signals1 = get_satisfied_signals(a1);
    EXPECT_EQ(signals0, ZX_SOCKET_WRITABLE | ZX_SOCKET_SHARE, "");
    EXPECT_EQ(signals1, ZX_SOCKET_WRITABLE | ZX_SOCKET_SHARE, "");

    zx_handle_close(a0);
    zx_handle_close(a1);
    zx_handle_close(b0);
    zx_handle_close(b1);
    zx_handle_close(c0);
    zx_handle_close(c1);

    END_TEST;
}

BEGIN_TEST_CASE(socket_tests)
RUN_TEST(socket_basic)
RUN_TEST(socket_signals)
RUN_TEST(socket_peer_closed)
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
RUN_TEST(socket_accept)
END_TEST_CASE(socket_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
