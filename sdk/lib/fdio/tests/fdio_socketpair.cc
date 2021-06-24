// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains tests for socketpair() behaviors specific to the
// fdio implementation. Cross-platform tests for socketpair() behaviors
// should go into socketpair.cc.

#ifndef __Fuchsia__
#error This file is Fuchsia-specific
#endif

#include <errno.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/zx/handle.h>
#include <poll.h>
#include <unistd.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "sdk/lib/fdio/tests/socketpair_test_helpers.h"
#include "src/lib/testing/predicates/status.h"

using fdio_tests::SocketPair;
using fdio_tests::TypeToString;

TEST_P(SocketPair, CloneOrUnwrapAndWrap) {
  zx::handle handle;
  ASSERT_OK(fdio_fd_clone(fds()[0].get(), handle.reset_and_get_address()));

  fbl::unique_fd cloned_fd;
  ASSERT_OK(fdio_fd_create(handle.release(), cloned_fd.reset_and_get_address()));

  ASSERT_OK(fdio_fd_transfer(mutable_fds()[0].release(), handle.reset_and_get_address()));

  fbl::unique_fd transferred_fd;
  ASSERT_OK(fdio_fd_create(handle.release(), transferred_fd.reset_and_get_address()));

  // Verify that an operation specific to socketpairs works on these fds.
  ASSERT_EQ(shutdown(transferred_fd.get(), SHUT_WR), 0) << strerror(errno);
  ASSERT_EQ(shutdown(cloned_fd.get(), SHUT_RD), 0) << strerror(errno);
}

TEST_P(SocketPair, WaitBeginEnd) {
  fdio_t* io = fdio_unsafe_fd_to_io(fds()[0].get());

  // fdio_unsafe_wait_begin

  zx::handle handle;
  {
    zx_signals_t signals;
    fdio_unsafe_wait_begin(io, POLLIN, handle.reset_and_get_address(), &signals);
    EXPECT_NE(handle.get(), ZX_HANDLE_INVALID);
    EXPECT_EQ(signals, ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_PEER_WRITE_DISABLED);
  }

  {
    zx_signals_t signals;
    fdio_unsafe_wait_begin(io, POLLOUT, handle.reset_and_get_address(), &signals);
    EXPECT_NE(handle.get(), ZX_HANDLE_INVALID);
    EXPECT_EQ(signals, ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_DISABLED);
  }

  {
    zx_signals_t signals;
    fdio_unsafe_wait_begin(io, POLLRDHUP, handle.reset_and_get_address(), &signals);
    EXPECT_NE(handle.get(), ZX_HANDLE_INVALID);
    EXPECT_EQ(signals, ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_PEER_WRITE_DISABLED);
  }

  {
    zx_signals_t signals;
    fdio_unsafe_wait_begin(io, POLLHUP, handle.reset_and_get_address(), &signals);
    EXPECT_NE(handle.get(), ZX_HANDLE_INVALID);
    EXPECT_EQ(signals, ZX_SIGNAL_NONE);
  }

  // fdio_unsafe_wait_end

  {
    uint32_t events;
    fdio_unsafe_wait_end(io, ZX_SOCKET_READABLE, &events);
    EXPECT_EQ(int32_t(events), POLLIN);
  }

  {
    uint32_t events;
    fdio_unsafe_wait_end(io, ZX_SOCKET_PEER_CLOSED, &events);
    EXPECT_EQ(int32_t(events), POLLIN | POLLRDHUP);
  }

  {
    uint32_t events;
    fdio_unsafe_wait_end(io, ZX_SOCKET_PEER_WRITE_DISABLED, &events);
    EXPECT_EQ(int32_t(events), POLLIN | POLLRDHUP);
  }

  {
    uint32_t events;
    fdio_unsafe_wait_end(io, ZX_SOCKET_WRITABLE, &events);
    EXPECT_EQ(int32_t(events), POLLOUT);
  }

  {
    uint32_t events;
    fdio_unsafe_wait_end(io, ZX_SOCKET_WRITE_DISABLED, &events);
    EXPECT_EQ(int32_t(events), POLLOUT);
  }

  fdio_unsafe_release(io);
}
