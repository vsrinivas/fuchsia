// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/hardware/pty/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/io.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/zx/time.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/status.h>

#include <unittest/unittest.h>

// returns an int to avoid sign errors from ASSERT_*()
static int fd_signals(const fbl::unique_fd& fd, uint32_t wait_for_any, zx::time deadline) {
  uint32_t signals = 0;
  fdio_wait_fd(fd.get(), wait_for_any, &signals, deadline.get());
  if (deadline != zx::time{}) {
    // If we waited for non-zero time, re-read with 0 time.  This call bottoms
    // out in zx_object_wait_one, which will return signals that were
    // transiently asserted during the wait.  The second call will allow us to
    // ignore signals that aren't currently asserted.
    fdio_wait_fd(fd.get(), wait_for_any, &signals, 0);
  }
  return signals;
}

static ssize_t write_full(int fd) {
  char tmp[300];
  memset(tmp, 0x33, sizeof(tmp));
  ssize_t total = 0;
  for (;;) {
    ssize_t r = write(fd, tmp, sizeof(tmp));
    if (r < 0) {
      if (errno == EAGAIN) {
        break;
      }
      return -errno;
    }
    if (r == 0) {
      break;
    }
    total += r;
  }
  return total;
}

static ssize_t read_all(int fd) {
  char tmp[700];
  ssize_t total = 0;
  for (;;) {
    ssize_t r = read(fd, tmp, sizeof(tmp));
    if (r < 0) {
      if (errno == EAGAIN) {
        break;
      }
      return -errno;
    }
    if (r == 0) {
      break;
    }
    for (int n = 0; n < r; n++) {
      if (tmp[n] != 0x33) {
        return -EFAULT;
      }
    }
    total += r;
  }
  return total;
}

static zx_status_t open_client(int fd, uint32_t client_id, int* out_fd) {
  if (!out_fd) {
    return ZX_ERR_INVALID_ARGS;
  }

  fdio_t* io = fdio_unsafe_fd_to_io(fd);
  if (!io) {
    return ZX_ERR_INTERNAL;
  }

  zx::channel device_channel, client_channel;
  zx_status_t status = zx::channel::create(0, &device_channel, &client_channel);
  if (status != ZX_OK) {
    return status;
  }

  zx_status_t fidl_status = fuchsia_hardware_pty_DeviceOpenClient(
      fdio_unsafe_borrow_channel(io), client_id, device_channel.release(), &status);
  if (status != ZX_OK) {
    return status;
  }
  fdio_unsafe_release(io);
  if (fidl_status != ZX_OK) {
    return fidl_status;
  }
  if (status != ZX_OK) {
    return status;
  }

  status = fdio_fd_create(client_channel.release(), out_fd);
  if (status != ZX_OK) {
    return status;
  }
  return fcntl(*out_fd, F_SETFL, O_NONBLOCK);
}

static bool pty_test(void) {
  BEGIN_TEST;

  // Connect to the PTY service.  We have to do this dance rather than just
  // using open() because open() uses the DESCRIBE flag internally, and the
  // plumbing of the PTY service through svchost causes the DESCRIBE to get
  // consumed by the wrong code, resulting in the wrong NodeInfo being provided.
  // This manifests as a loss of fd signals.
  fbl::unique_fd ps;
  {
    zx::channel local, remote;
    ASSERT_EQ(zx::channel::create(0, &local, &remote), ZX_OK);
    ASSERT_EQ(fdio_service_connect("/svc/fuchsia.hardware.pty.Device", remote.release()), ZX_OK);
    int fd;
    ASSERT_EQ(fdio_fd_create(local.release(), &fd), ZX_OK);
    ps.reset(fd);
    ASSERT_TRUE(ps.is_valid());
    int flags = fcntl(ps.get(), F_GETFL);
    ASSERT_GE(flags, 0);
    ASSERT_EQ(fcntl(ps.get(), F_SETFL, flags | O_NONBLOCK), 0);
  }

  fdio_cpp::UnownedFdioCaller ps_io(ps.get());

  int pc_fd;
  ASSERT_EQ(open_client(ps.get(), 0, &pc_fd), ZX_OK);
  ASSERT_GE(pc_fd, 0, "");

  fbl::unique_fd pc(pc_fd);
  ASSERT_EQ(bool(pc), true, "");

  fdio_cpp::UnownedFdioCaller pc_io(pc.get());

  char tmp[32];

  ASSERT_EQ(fd_signals(ps, POLLOUT, zx::time{}), POLLOUT, "");
  ASSERT_EQ(fd_signals(pc, POLLOUT, zx::time{}), POLLOUT, "");

  // nothing to read
  ASSERT_EQ(read(ps.get(), tmp, 32), -1, "");
  ASSERT_EQ(errno, EAGAIN, "");
  ASSERT_EQ(read(pc.get(), tmp, 32), -1, "");
  ASSERT_EQ(errno, EAGAIN, "");

  // write server, read client
  ASSERT_EQ(write(ps.get(), "xyzzy", 5), 5, "");
  ASSERT_EQ(fd_signals(pc, POLLIN | POLLOUT, zx::time{}), POLLIN | POLLOUT, "");

  memset(tmp, 0xee, 5);
  ASSERT_EQ(read(pc.get(), tmp, 5), 5, "");
  ASSERT_EQ(memcmp(tmp, "xyzzy", 5), 0, "");
  ASSERT_EQ(fd_signals(pc, POLLOUT, zx::time{}), POLLOUT, "");

  // write client, read server
  ASSERT_EQ(write(pc.get(), "xyzzy", 5), 5, "");
  ASSERT_EQ(fd_signals(ps, POLLIN | POLLOUT, zx::time{}), POLLIN | POLLOUT, "");

  memset(tmp, 0xee, 5);
  ASSERT_EQ(read(ps.get(), tmp, 5), 5, "");
  ASSERT_EQ(memcmp(tmp, "xyzzy", 5), 0, "");
  ASSERT_EQ(fd_signals(ps, POLLOUT, zx::time{}), POLLOUT, "");

  // write server until full, then drain
  ASSERT_EQ(write_full(ps.get()), 4096, "");
  ASSERT_EQ(fd_signals(ps, 0, zx::time{}), 0, "");
  ASSERT_EQ(read_all(pc.get()), 4096, "");
  ASSERT_EQ(fd_signals(ps, POLLOUT, zx::time{}), POLLOUT, "");

  // write client until full, then drain
  ASSERT_EQ(write_full(pc.get()), 4096, "");
  ASSERT_EQ(fd_signals(pc, 0, zx::time{}), 0, "");
  ASSERT_EQ(read_all(ps.get()), 4096, "");
  ASSERT_EQ(fd_signals(pc, POLLOUT, zx::time{}), POLLOUT, "");

  // verify no events pending
  uint32_t events;
  zx_status_t status;

  ASSERT_EQ(fuchsia_hardware_pty_DeviceReadEvents(pc_io.borrow_channel(), &status, &events), ZX_OK,
            "");
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(events, 0u, "");

  // write a ctrl-c
  ASSERT_EQ(write(ps.get(), "\x03", 1), 1, "");

  // should be an event now
  ASSERT_EQ(fuchsia_hardware_pty_DeviceReadEvents(pc_io.borrow_channel(), &status, &events), ZX_OK,
            "");
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(events, fuchsia_hardware_pty_EVENT_INTERRUPT, "");

  // should vanish once we read it
  ASSERT_EQ(fuchsia_hardware_pty_DeviceReadEvents(pc_io.borrow_channel(), &status, &events), ZX_OK,
            "");
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(events, 0u, "");

  // write something containing a special char
  // should write up to and including the special char
  // converting the special char to a signal
  ASSERT_EQ(write(ps.get(), "hello\x03world", 11), 6, "");
  ASSERT_EQ(read(pc.get(), tmp, 6), 5, "");
  ASSERT_EQ(memcmp(tmp, "hello", 5), 0, "");
  ASSERT_EQ(fuchsia_hardware_pty_DeviceReadEvents(pc_io.borrow_channel(), &status, &events), ZX_OK,
            "");
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(events, fuchsia_hardware_pty_EVENT_INTERRUPT, "");

  fuchsia_hardware_pty_WindowSize ws;
  ASSERT_EQ(fuchsia_hardware_pty_DeviceGetWindowSize(pc_io.borrow_channel(), &status, &ws), ZX_OK,
            "");
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(ws.width, 0u, "");
  ASSERT_EQ(ws.height, 0u, "");
  ws.width = 80;
  ws.height = 25;
  ASSERT_EQ(fuchsia_hardware_pty_DeviceSetWindowSize(ps_io.borrow_channel(), &ws, &status), ZX_OK,
            "");
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(fuchsia_hardware_pty_DeviceGetWindowSize(pc_io.borrow_channel(), &status, &ws), ZX_OK,
            "");
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(ws.width, 80u, "");
  ASSERT_EQ(ws.height, 25u, "");

  // verify that we don't get events for special chars in raw mode
  uint32_t features;
  ASSERT_EQ(fuchsia_hardware_pty_DeviceClrSetFeature(
                pc_io.borrow_channel(), 0, fuchsia_hardware_pty_FEATURE_RAW, &status, &features),
            ZX_OK, "");
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(features & fuchsia_hardware_pty_FEATURE_RAW, fuchsia_hardware_pty_FEATURE_RAW, "");
  ASSERT_EQ(write(ps.get(), "\x03", 1), 1, "");
  ASSERT_EQ(read(pc.get(), tmp, 1), 1, "");
  ASSERT_EQ(tmp[0], '\x03', "");
  ASSERT_EQ(fuchsia_hardware_pty_DeviceReadEvents(pc_io.borrow_channel(), &status, &events), ZX_OK,
            "");
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(events, 0u, "");

  // create a second client
  int pc1_fd;
  ASSERT_EQ(open_client(pc.get(), 1, &pc1_fd), ZX_OK);
  ASSERT_GE(pc1_fd, 0, "");

  fbl::unique_fd pc1(pc1_fd);
  ASSERT_EQ(bool(pc1), true, "");

  fdio_cpp::UnownedFdioCaller pc1_io(pc1.get());

  // reads/writes to non-active client should block
  ASSERT_EQ(fd_signals(pc1, 0, zx::time{}), 0, "");
  ASSERT_EQ(write(pc1.get(), "test", 4), -1, "");
  ASSERT_EQ(errno, EAGAIN, "");
  ASSERT_EQ(read(pc1.get(), tmp, 4), -1, "");
  ASSERT_EQ(errno, EAGAIN, "");

  uint32_t n = 2;
  ASSERT_EQ(fuchsia_hardware_pty_DeviceMakeActive(pc_io.borrow_channel(), n, &status), ZX_OK, "");
  ASSERT_EQ(status, ZX_ERR_NOT_FOUND, "");

  // non-controlling client cannot change active client
  ASSERT_EQ(fuchsia_hardware_pty_DeviceMakeActive(pc1_io.borrow_channel(), n, &status), ZX_OK, "");
  ASSERT_EQ(status, ZX_ERR_ACCESS_DENIED, "");

  // but controlling client can
  n = 1;
  ASSERT_EQ(fuchsia_hardware_pty_DeviceMakeActive(pc_io.borrow_channel(), n, &status), ZX_OK, "");
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(fd_signals(pc, 0, zx::time{}), 0, "");
  ASSERT_EQ(fd_signals(pc1, POLLOUT, zx::time{}), POLLOUT, "");
  ASSERT_EQ(write(pc1.get(), "test", 4), 4, "");
  ASSERT_EQ(read(ps.get(), tmp, 4), 4, "");
  ASSERT_EQ(memcmp(tmp, "test", 4), 0, "");

  // make sure controlling client observes departing active client
  pc1_io.reset();
  pc1.reset();
  ASSERT_EQ(fd_signals(pc, POLLHUP | POLLPRI, zx::time::infinite()), POLLHUP | POLLPRI, "");
  ASSERT_EQ(fuchsia_hardware_pty_DeviceReadEvents(pc_io.borrow_channel(), &status, &events), ZX_OK,
            "");
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(events, fuchsia_hardware_pty_EVENT_HANGUP, "");

  // verify that server observes departure of last client
  pc_io.reset();
  pc.reset();
  ASSERT_EQ(fd_signals(ps, POLLHUP | POLLIN, zx::time::infinite()), POLLHUP | POLLIN, "");

  ps_io.reset();
  ps.reset();

  END_TEST;
}

bool not_a_pty_test(void) {
  BEGIN_TEST;

  fbl::unique_fd root_dir(open("/", O_DIRECTORY | O_RDONLY));
  ASSERT_EQ(bool(root_dir), true, "");

  fdio_cpp::UnownedFdioCaller io(root_dir.get());

  // Sending pty messages such as 'get window size' should fail
  // properly on things that are not ptys.
  fuchsia_hardware_pty_WindowSize ws;
  zx_status_t status;
  ASSERT_EQ(fuchsia_hardware_pty_DeviceGetWindowSize(io.borrow_channel(), &status, &ws),
            ZX_ERR_BAD_HANDLE, "");

  io.reset();

  END_TEST;
}

BEGIN_TEST_CASE(pty_tests)
RUN_TEST(pty_test)
RUN_TEST(not_a_pty_test)
END_TEST_CASE(pty_tests)
