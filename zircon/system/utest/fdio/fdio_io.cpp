// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/unique_fd.h>
#include <lib/fdio/io.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/socket.h>

#include <unittest/unittest.h>

static bool wait_fd_test() {
    BEGIN_TEST;

    zx::socket pipe;
    int raw_fd = -1;
    EXPECT_EQ(ZX_OK, fdio_pipe_half2(&raw_fd, pipe.reset_and_get_address()));
    EXPECT_LE(0, raw_fd);
    fbl::unique_fd fd(raw_fd);

    uint32_t pending = 0;
    EXPECT_EQ(ZX_ERR_TIMED_OUT, fdio_wait_fd(fd.get(), FDIO_EVT_READABLE, &pending,
                                             ZX_TIME_INFINITE_PAST));

    pending = 0;
    EXPECT_EQ(ZX_OK, fdio_wait_fd(fd.get(), FDIO_EVT_WRITABLE, &pending, ZX_TIME_INFINITE_PAST));
    EXPECT_TRUE(pending & FDIO_EVT_WRITABLE);

    EXPECT_EQ(ZX_OK, pipe.write(0, "abc", 3, nullptr));
    pending = 0;
    EXPECT_EQ(ZX_OK, fdio_wait_fd(fd.get(), FDIO_EVT_READABLE, &pending, ZX_TIME_INFINITE_PAST));
    EXPECT_TRUE(pending & FDIO_EVT_READABLE);

    pending = 0;
    EXPECT_EQ(ZX_ERR_TIMED_OUT, fdio_wait_fd(fd.get(), FDIO_EVT_PEER_CLOSED, &pending,
                                             ZX_TIME_INFINITE_PAST));

    pipe.reset();
    pending = 0;
    EXPECT_EQ(ZX_OK, fdio_wait_fd(fd.get(), FDIO_EVT_PEER_CLOSED, &pending, ZX_TIME_INFINITE_PAST));
    EXPECT_TRUE(pending & FDIO_EVT_PEER_CLOSED);

    END_TEST;
}

static bool handle_fd_test() {
    BEGIN_TEST;

    constexpr zx_signals_t in_signals = ZX_USER_SIGNAL_0;
    constexpr zx_signals_t out_signal_a = ZX_USER_SIGNAL_1;
    constexpr zx_signals_t out_signal_b = ZX_USER_SIGNAL_2;
    constexpr zx_signals_t out_signals = out_signal_a | out_signal_b;

    zx::eventpair e1, e2;
    ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &e1, &e2));

    fbl::unique_fd fd(fdio_handle_fd(e1.release(), in_signals, out_signals, false));
    EXPECT_LE(0, fd.get());

    uint32_t pending = 0;
    EXPECT_EQ(ZX_ERR_TIMED_OUT, fdio_wait_fd(fd.get(), FDIO_EVT_READABLE, &pending,
                                             ZX_TIME_INFINITE_PAST));

    pending = 0;
    EXPECT_EQ(ZX_ERR_TIMED_OUT, fdio_wait_fd(fd.get(), FDIO_EVT_WRITABLE, &pending,
                                             ZX_TIME_INFINITE_PAST));

    EXPECT_EQ(ZX_OK, e2.signal_peer(0, in_signals));

    pending = 0;
    EXPECT_EQ(ZX_OK, fdio_wait_fd(fd.get(), FDIO_EVT_READABLE, &pending, ZX_TIME_INFINITE_PAST));
    EXPECT_TRUE(pending & FDIO_EVT_READABLE);

    pending = 0;
    EXPECT_EQ(ZX_ERR_TIMED_OUT, fdio_wait_fd(fd.get(), FDIO_EVT_WRITABLE, &pending,
                                             ZX_TIME_INFINITE_PAST));

    EXPECT_EQ(ZX_OK, e2.signal_peer(0, out_signal_a));

    pending = 0;
    EXPECT_EQ(ZX_OK, fdio_wait_fd(fd.get(), FDIO_EVT_READABLE, &pending, ZX_TIME_INFINITE_PAST));
    EXPECT_TRUE(pending & FDIO_EVT_READABLE);

    pending = 0;
    EXPECT_EQ(ZX_OK, fdio_wait_fd(fd.get(), FDIO_EVT_WRITABLE, &pending, ZX_TIME_INFINITE_PAST));
    EXPECT_TRUE(pending & FDIO_EVT_WRITABLE);

    EXPECT_EQ(ZX_OK, e2.signal_peer(in_signals | out_signal_a, out_signal_b));

    pending = 0;
    EXPECT_EQ(ZX_ERR_TIMED_OUT, fdio_wait_fd(fd.get(), FDIO_EVT_READABLE, &pending,
                                             ZX_TIME_INFINITE_PAST));

    pending = 0;
    EXPECT_EQ(ZX_OK, fdio_wait_fd(fd.get(), FDIO_EVT_WRITABLE, &pending, ZX_TIME_INFINITE_PAST));
    EXPECT_TRUE(pending & FDIO_EVT_WRITABLE);

    fd.reset();

    zx_signals_t observed = 0;
    ASSERT_EQ(ZX_OK, e2.wait_one(ZX_EVENTPAIR_PEER_CLOSED, zx::time::infinite_past(), &observed));
    ASSERT_TRUE(observed & ZX_EVENTPAIR_PEER_CLOSED);

    END_TEST;
}

static bool handle_fd_share_test() {
    BEGIN_TEST;

    zx::eventpair e1, e2;
    ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &e1, &e2));

    fbl::unique_fd fd(fdio_handle_fd(e1.release(), ZX_USER_SIGNAL_0,
                                     ZX_USER_SIGNAL_1 | ZX_USER_SIGNAL_2, true));
    EXPECT_LE(0, fd.get());
    fd.reset();

    zx_signals_t observed = 0;
    ASSERT_EQ(ZX_ERR_TIMED_OUT, e2.wait_one(ZX_EVENTPAIR_PEER_CLOSED, zx::time::infinite_past(),
                                            &observed));

    END_TEST;
}

BEGIN_TEST_CASE(fdio_io_test)
RUN_TEST(wait_fd_test)
RUN_TEST(handle_fd_test)
RUN_TEST(handle_fd_share_test)
END_TEST_CASE(fdio_io_test)
