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

    // ensure parameter validation works
    EXPECT_EQ(zx_fifo_create(0, 0, 0, &a, &b), ZX_ERR_OUT_OF_RANGE, ""); // too small
    EXPECT_EQ(zx_fifo_create(35, 32, 0, &a, &b), ZX_ERR_OUT_OF_RANGE, ""); // not power of two
    EXPECT_EQ(zx_fifo_create(128, 33, 0, &a, &b), ZX_ERR_OUT_OF_RANGE, ""); // too large
    EXPECT_EQ(zx_fifo_create(0, 0, 1, &a, &b), ZX_ERR_OUT_OF_RANGE, ""); // invalid options

    // simple 8 x 8 fifo
    EXPECT_EQ(zx_fifo_create(8, 8, 0, &a, &b), ZX_OK, "");
    EXPECT_SIGNALS(a, ZX_FIFO_WRITABLE | ZX_SIGNAL_LAST_HANDLE);
    EXPECT_SIGNALS(b, ZX_FIFO_WRITABLE | ZX_SIGNAL_LAST_HANDLE);

    // should not be able to read any entries from an empty fifo
    uint32_t actual;
    EXPECT_EQ(zx_fifo_read(a, n, sizeof(n), &actual), ZX_ERR_SHOULD_WAIT, "");

    // should be able to write all entries into empty fifo
    ASSERT_EQ(zx_fifo_write(a, n, sizeof(n), &actual), ZX_OK, "");
    ASSERT_EQ(actual, 8u, "");
    EXPECT_SIGNALS(b, ZX_FIFO_READABLE | ZX_FIFO_WRITABLE | ZX_SIGNAL_LAST_HANDLE);

    // should be able to write no entries into a full fifo
    ASSERT_EQ(zx_fifo_write(a, n, sizeof(n), &actual), ZX_ERR_SHOULD_WAIT, "");
    EXPECT_SIGNALS(a, ZX_SIGNAL_LAST_HANDLE);

    // read half the entries, make sure they're what we expect
    memset(n, 0, sizeof(n));
    EXPECT_EQ(zx_fifo_read(b, n, sizeof(n) / 2, &actual), ZX_OK, "");
    ASSERT_EQ(actual, 4u, "");
    ASSERT_EQ(n[0], 1u, "");
    ASSERT_EQ(n[1], 2u, "");
    ASSERT_EQ(n[2], 3u, "");
    ASSERT_EQ(n[3], 4u, "");

    // should be writable again now
    EXPECT_SIGNALS(a, ZX_FIFO_WRITABLE | ZX_SIGNAL_LAST_HANDLE);

    // write some more, wrapping to the front again
    n[0] = 9u;
    n[1] = 10u;
    ASSERT_EQ(zx_fifo_write(a, n, sizeof(uint64_t) * 2, &actual), ZX_OK, "");
    ASSERT_EQ(actual, 2u, "");

    // read across the wrap, test partial read
    ASSERT_EQ(zx_fifo_read(b, n, sizeof(n), &actual), ZX_OK, "");
    ASSERT_EQ(actual, 6u, "");
    ASSERT_EQ(n[0], 5u, "");
    ASSERT_EQ(n[1], 6u, "");
    ASSERT_EQ(n[2], 7u, "");
    ASSERT_EQ(n[3], 8u, "");
    ASSERT_EQ(n[4], 9u, "");
    ASSERT_EQ(n[5], 10u, "");

    // should no longer be readable
    EXPECT_SIGNALS(b, ZX_FIFO_WRITABLE | ZX_SIGNAL_LAST_HANDLE);

    // write across the wrap
    n[0] = 11u; n[1] = 12u; n[2] = 13u; n[3] = 14u; n[4] = 15u;
    ASSERT_EQ(zx_fifo_write(a, n, sizeof(uint64_t) * 5, &actual), ZX_OK, "");
    ASSERT_EQ(actual, 5u, "");

    // partial write test
    n[0] = 16u; n[1] = 17u; n[2] = 18u;
    ASSERT_EQ(zx_fifo_write(a, n, sizeof(n), &actual), ZX_OK, "");
    ASSERT_EQ(actual, 3u, "");

    // small reads
    for (unsigned i = 0; i < 8; i++) {
        ASSERT_EQ(zx_fifo_read(b, n, sizeof(uint64_t), &actual), ZX_OK, "");
        ASSERT_EQ(actual, 1u, "");
        ASSERT_EQ(n[0], 11u + i, "");
    }

    zx_handle_close(b);
    EXPECT_SIGNALS(a, ZX_FIFO_PEER_CLOSED | ZX_SIGNAL_LAST_HANDLE);

    zx_handle_close(a);

    END_TEST;
}

static bool options_test(void) {
    BEGIN_TEST;

    zx_handle_t fifos[2];
    ASSERT_EQ(zx_fifo_create(23, 8, 8, &fifos[0], &fifos[1]),
              ZX_ERR_INVALID_ARGS, "");

    END_TEST;
}

BEGIN_TEST_CASE(fifo_tests)
RUN_TEST(basic_test)
END_TEST_CASE(fifo_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
