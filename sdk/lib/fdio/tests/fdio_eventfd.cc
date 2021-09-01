// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.io2/cpp/wire.h>
#include <lib/fdio/unsafe.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/types.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

// This file contains FDIO-specific tests for eventfd behaviors.

TEST(EventFDTest, WaitBeginTest) {
  fbl::unique_fd fd(eventfd(42, EFD_SEMAPHORE | EFD_NONBLOCK));
  EXPECT_TRUE(fd.is_valid());

  fdio_t* io = fdio_unsafe_fd_to_io(fd.get());
  ASSERT_NE(io, nullptr);

  constexpr auto kSignalReadable =
      static_cast<zx_signals_t>(fuchsia_io2::wire::DeviceSignal::kReadable);
  constexpr auto kSignalWritable =
      static_cast<zx_signals_t>(fuchsia_io2::wire::DeviceSignal::kWritable);

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

    EXPECT_EQ(signals, kSignalReadable, "Actual signals 0x%x", signals);
  }

  {
    uint32_t events = POLLOUT;
    zx_handle_t handle = ZX_HANDLE_INVALID;
    zx_signals_t signals = 0;
    fdio_unsafe_wait_begin(io, events, &handle, &signals);

    EXPECT_EQ(signals, kSignalWritable, "Actual signals 0x%x", signals);
  }

  {
    uint32_t events = POLLIN | POLLOUT;
    zx_handle_t handle = ZX_HANDLE_INVALID;
    zx_signals_t signals = 0;
    fdio_unsafe_wait_begin(io, events, &handle, &signals);

    EXPECT_EQ(signals, kSignalReadable | kSignalWritable, "Actual signals 0x%x", signals);
  }

  {
    uint32_t events = POLLPRI;
    zx_handle_t handle = ZX_HANDLE_INVALID;
    zx_signals_t signals = 0;
    fdio_unsafe_wait_begin(io, events, &handle, &signals);

    EXPECT_EQ(signals, 0, "Actual signals 0x%x", signals);
  }

  fdio_unsafe_release(io);
}
