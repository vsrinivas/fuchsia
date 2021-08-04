// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/unsafe.h>
#include <poll.h>
#include <unistd.h>

#include <thread>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "predicates.h"

TEST(Pipe, WaitBegin) {
  int fds[2];
  ASSERT_SUCCESS(pipe(fds));

  fdio_t* io = fdio_unsafe_fd_to_io(fds[0]);
  ASSERT_NE(io, nullptr);

  constexpr zx_signals_t kExpectedPollInSignals =
      ZX_SOCKET_READABLE |            // Data is available to read
      ZX_SOCKET_PEER_CLOSED |         // Peer is closed, either data is readable or we've hit EOF
      ZX_SOCKET_PEER_WRITE_DISABLED;  // Peer can't write any more, either data is readable or
                                      // we've hit EOF

  // TODO(https://fxbug.dev/47132): Understand why this does not include ZX_SOCKET_PEER_CLOSED.
  constexpr zx_signals_t kExpectedPollOutSignals = ZX_SOCKET_WRITABLE |       // Data can be written
                                                   ZX_SOCKET_WRITE_DISABLED;  // Write is disabled

  {
    uint32_t events = 0;
    zx_handle_t handle = ZX_HANDLE_INVALID;
    zx_signals_t signals = 0;
    fdio_unsafe_wait_begin(io, events, &handle, &signals);

    EXPECT_EQ(signals, 0, "Actual signals 0x%x", signals);
  }

  {
    uint32_t events = POLLIN;
    zx_handle_t handle = ZX_HANDLE_INVALID;
    zx_signals_t signals = 0;
    fdio_unsafe_wait_begin(io, events, &handle, &signals);

    EXPECT_EQ(signals, kExpectedPollInSignals, "Actual signals 0x%x", signals);
  }

  {
    uint32_t events = POLLOUT;
    zx_handle_t handle = ZX_HANDLE_INVALID;
    zx_signals_t signals = 0;
    fdio_unsafe_wait_begin(io, events, &handle, &signals);

    EXPECT_EQ(signals, kExpectedPollOutSignals, "Actual signals 0x%x", signals);
  }

  {
    uint32_t events = POLLIN | POLLOUT;
    zx_handle_t handle = ZX_HANDLE_INVALID;
    zx_signals_t signals = 0;
    fdio_unsafe_wait_begin(io, events, &handle, &signals);

    EXPECT_EQ(signals, kExpectedPollInSignals | kExpectedPollOutSignals, "Actual signals 0x%x",
              signals);
  }

  {
    uint32_t events = POLLPRI;
    zx_handle_t handle = ZX_HANDLE_INVALID;
    zx_signals_t signals = 0;
    fdio_unsafe_wait_begin(io, events, &handle, &signals);

    // POLLPRI is not supported for pipes.
    EXPECT_EQ(signals, 0, "Actual signals 0x%x", signals);
  }

  fdio_unsafe_release(io);
}
