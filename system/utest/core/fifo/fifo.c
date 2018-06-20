// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <unistd.h>

#include <zircon/syscalls.h>
#include <unittest/unittest.h>

static zx_signals_t get_signals(zx_handle_t h) {
    zx_signals_t pending;
    zx_status_t status = zx_object_wait_one(h, 0xFFFFFFFF, 0u, &pending);
    if ((status != ZX_OK) && (status != ZX_ERR_TIMED_OUT)) {
        return 0xFFFFFFFF;
    }
    return pending;
}

#define EXPECT_SIGNALS(h, s) EXPECT_EQ(get_signals(h), s, "")

static bool basic_test(void) {
    BEGIN_TEST;
    zx_handle_t a, b;
    uint64_t n[8] = { 1, 2, 3, 4, 5, 6, 7, 8};
    enum { ELEM_SZ = sizeof(n[0]) };

    // ensure parameter validation works
    EXPECT_EQ(zx_fifo_create(0, 0, 0, &a, &b), ZX_ERR_OUT_OF_RANGE, ""); // too small
    EXPECT_EQ(zx_fifo_create(35, 32, 0, &a, &b), ZX_ERR_OUT_OF_RANGE, ""); // not power of two
    EXPECT_EQ(zx_fifo_create(128, 33, 0, &a, &b), ZX_ERR_OUT_OF_RANGE, ""); // too large
    EXPECT_EQ(zx_fifo_create(0, 0, 1, &a, &b), ZX_ERR_OUT_OF_RANGE, ""); // invalid options

    // simple 8 x 8 fifo
    EXPECT_EQ(zx_fifo_create(8, ELEM_SZ, 0, &a, &b), ZX_OK, "");
    EXPECT_SIGNALS(a, ZX_FIFO_WRITABLE);
    EXPECT_SIGNALS(b, ZX_FIFO_WRITABLE);

    // Check that koids line up.
    zx_info_handle_basic_t info[2] = {};
    zx_status_t status = zx_object_get_info(a, ZX_INFO_HANDLE_BASIC, &info[0], sizeof(info[0]), NULL, NULL);
    ASSERT_EQ(status, ZX_OK, "");
    status = zx_object_get_info(b, ZX_INFO_HANDLE_BASIC, &info[1], sizeof(info[1]), NULL, NULL);
    ASSERT_EQ(status, ZX_OK, "");
    ASSERT_NE(info[0].koid, 0u, "zero koid!");
    ASSERT_NE(info[0].related_koid, 0u, "zero peer koid!");
    ASSERT_NE(info[1].koid, 0u, "zero koid!");
    ASSERT_NE(info[1].related_koid, 0u, "zero peer koid!");
    ASSERT_EQ(info[0].koid, info[1].related_koid, "mismatched koids!");
    ASSERT_EQ(info[1].koid, info[0].related_koid, "mismatched koids!");

    // should not be able to read any entries from an empty fifo
    size_t actual;
    EXPECT_EQ(zx_fifo_read(a, ELEM_SZ, n, 8, &actual), ZX_ERR_SHOULD_WAIT, "");

    // not allowed to read or write zero elements
    EXPECT_EQ(zx_fifo_read(a, ELEM_SZ, n, 0, &actual), ZX_ERR_OUT_OF_RANGE, "");
    EXPECT_EQ(zx_fifo_write(a, ELEM_SZ, n, 0, &actual), ZX_ERR_OUT_OF_RANGE, "");

    // element size must match
    EXPECT_EQ(zx_fifo_read(a, ELEM_SZ + 1, n, 8, &actual), ZX_ERR_OUT_OF_RANGE, "");
    EXPECT_EQ(zx_fifo_write(a, ELEM_SZ + 1, n, 8, &actual), ZX_ERR_OUT_OF_RANGE, "");

    // should be able to write all entries into empty fifo
    ASSERT_EQ(zx_fifo_write(a, ELEM_SZ, n, 8, &actual), ZX_OK, "");
    ASSERT_EQ(actual, 8u, "");
    EXPECT_SIGNALS(b, ZX_FIFO_READABLE | ZX_FIFO_WRITABLE);

    // should be able to write no entries into a full fifo
    ASSERT_EQ(zx_fifo_write(a, ELEM_SZ, n, 8, &actual), ZX_ERR_SHOULD_WAIT, "");
    EXPECT_SIGNALS(a, 0u);

    // read half the entries, make sure they're what we expect
    memset(n, 0, sizeof(n));
    EXPECT_EQ(zx_fifo_read(b, ELEM_SZ, n, 4, &actual), ZX_OK, "");
    ASSERT_EQ(actual, 4u, "");
    ASSERT_EQ(n[0], 1u, "");
    ASSERT_EQ(n[1], 2u, "");
    ASSERT_EQ(n[2], 3u, "");
    ASSERT_EQ(n[3], 4u, "");

    // should be writable again now
    EXPECT_SIGNALS(a, ZX_FIFO_WRITABLE);

    // write some more, wrapping to the front again
    n[0] = 9u;
    n[1] = 10u;
    ASSERT_EQ(zx_fifo_write(a, ELEM_SZ, n, 2, &actual), ZX_OK, "");
    ASSERT_EQ(actual, 2u, "");

    // read across the wrap, test partial read
    ASSERT_EQ(zx_fifo_read(b, ELEM_SZ, n, 8, &actual), ZX_OK, "");
    ASSERT_EQ(actual, 6u, "");
    ASSERT_EQ(n[0], 5u, "");
    ASSERT_EQ(n[1], 6u, "");
    ASSERT_EQ(n[2], 7u, "");
    ASSERT_EQ(n[3], 8u, "");
    ASSERT_EQ(n[4], 9u, "");
    ASSERT_EQ(n[5], 10u, "");

    // should no longer be readable
    EXPECT_SIGNALS(b, ZX_FIFO_WRITABLE);

    // write across the wrap
    n[0] = 11u; n[1] = 12u; n[2] = 13u; n[3] = 14u; n[4] = 15u;
    ASSERT_EQ(zx_fifo_write(a, ELEM_SZ, n, 5, &actual), ZX_OK, "");
    ASSERT_EQ(actual, 5u, "");

    // partial write test
    n[0] = 16u; n[1] = 17u; n[2] = 18u;
    ASSERT_EQ(zx_fifo_write(a, ELEM_SZ, n, 5, &actual), ZX_OK, "");
    ASSERT_EQ(actual, 3u, "");

    // small reads
    for (unsigned i = 0; i < 8; i++) {
        ASSERT_EQ(zx_fifo_read(b, ELEM_SZ, n, 1, &actual), ZX_OK, "");
        ASSERT_EQ(actual, 1u, "");
        ASSERT_EQ(n[0], 11u + i, "");
    }

    // write and then close, verify we can read written entries before
    // receiving ZX_ERR_PEER_CLOSED.
    n[0] = 19u;
    ASSERT_EQ(zx_fifo_write(b, ELEM_SZ, n, 1, &actual), ZX_OK, "");
    ASSERT_EQ(actual, 1u, "");
    zx_handle_close(b);
    EXPECT_SIGNALS(a, ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED);
    ASSERT_EQ(zx_fifo_read(a, ELEM_SZ, n, 8, &actual), ZX_OK, "");
    ASSERT_EQ(actual, 1u, "");
    EXPECT_SIGNALS(a, ZX_FIFO_PEER_CLOSED);
    ASSERT_EQ(zx_fifo_read(a, ELEM_SZ, n, 8, &actual), ZX_ERR_PEER_CLOSED, "");

    zx_handle_close(a);

    END_TEST;
}

static bool peer_closed_test(void) {
    BEGIN_TEST;

    zx_handle_t fifo[2];
    ASSERT_EQ(zx_fifo_create(16, 16, 0, &fifo[0], &fifo[1]), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(fifo[1]), ZX_OK, "");
    ASSERT_EQ(zx_object_signal_peer(fifo[0], 0u, ZX_USER_SIGNAL_0), ZX_ERR_PEER_CLOSED, "");
    ASSERT_EQ(zx_handle_close(fifo[0]), ZX_OK, "");

    END_TEST;
}

static bool options_test(void) {
    BEGIN_TEST;

    zx_handle_t fifos[2];
    ASSERT_EQ(zx_fifo_create(23, 8, 8, &fifos[0], &fifos[1]),
              ZX_ERR_OUT_OF_RANGE, "");

    END_TEST;
}

BEGIN_TEST_CASE(fifo_tests)
RUN_TEST(basic_test)
RUN_TEST(peer_closed_test)
RUN_TEST(options_test)
END_TEST_CASE(fifo_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
