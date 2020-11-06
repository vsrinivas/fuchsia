// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/hardware/pty/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/io.h>
#include <lib/zx/time.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/status.h>

#include <zxtest/zxtest.h>

namespace fpty = ::llcpp::fuchsia::hardware::pty;

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
    fdio_unsafe_release(io);
    return status;
  }

  auto result = fpty::Device::Call::OpenClient(zx::unowned_channel(fdio_unsafe_borrow_channel(io)),
                                               client_id, std::move(device_channel));

  fdio_unsafe_release(io);

  if (result.status() != ZX_OK) {
    return result.status();
  }
  if (result->s != ZX_OK) {
    return result->s;
  }

  status = fdio_fd_create(client_channel.release(), out_fd);
  if (status != ZX_OK) {
    return status;
  }
  return fcntl(*out_fd, F_SETFL, O_NONBLOCK);
}

TEST(PtyTests, pty_test) {
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
  auto result1 = fpty::Device::Call::ReadEvents(pc_io.channel());

  ASSERT_EQ(result1.status(), ZX_OK, "");
  ASSERT_EQ(result1->status, ZX_OK, "");
  ASSERT_EQ(result1->events, 0u, "");

  // write a ctrl-c
  ASSERT_EQ(write(ps.get(), "\x03", 1), 1, "");

  // should be an event now
  auto result2 = fpty::Device::Call::ReadEvents(pc_io.channel());
  ASSERT_EQ(result2.status(), ZX_OK, "");
  ASSERT_EQ(result2->status, ZX_OK, "");
  ASSERT_EQ(result2->events, fpty::EVENT_INTERRUPT, "");

  // should vanish once we read it
  auto result3 = fpty::Device::Call::ReadEvents(pc_io.channel());
  ASSERT_EQ(result3.status(), ZX_OK, "");
  ASSERT_EQ(result3->status, ZX_OK, "");
  ASSERT_EQ(result3->events, 0u, "");

  // write something containing a special char
  // should write up to and including the special char
  // converting the special char to a signal
  ASSERT_EQ(write(ps.get(), "hello\x03world", 11), 6, "");
  ASSERT_EQ(read(pc.get(), tmp, 6), 5, "");
  ASSERT_EQ(memcmp(tmp, "hello", 5), 0, "");
  auto result4 = fpty::Device::Call::ReadEvents(pc_io.channel());
  ASSERT_EQ(result4.status(), ZX_OK, "");
  ASSERT_EQ(result4->status, ZX_OK, "");
  ASSERT_EQ(result4->events, fpty::EVENT_INTERRUPT, "");

  auto ws_result1 = fpty::Device::Call::GetWindowSize(pc_io.channel());
  ASSERT_EQ(ws_result1.status(), ZX_OK, "");
  ASSERT_EQ(ws_result1->status, ZX_OK, "");
  ASSERT_EQ(ws_result1->size.width, 0u, "");
  ASSERT_EQ(ws_result1->size.height, 0u, "");

  fpty::WindowSize ws;
  ws.width = 80;
  ws.height = 25;
  auto result5 = fpty::Device::Call::SetWindowSize(pc_io.channel(), ws);
  ASSERT_EQ(result5.status(), ZX_OK, "");
  ASSERT_EQ(result5->status, ZX_OK, "");
  auto ws_result2 = fpty::Device::Call::GetWindowSize(pc_io.channel());
  ASSERT_EQ(ws_result2.status(), ZX_OK, "");
  ASSERT_EQ(ws_result2->status, ZX_OK, "");
  ASSERT_EQ(ws_result2->size.width, 80u, "");
  ASSERT_EQ(ws_result2->size.height, 25u, "");

  // verify that we don't get events for special chars in raw mode
  auto result6 = fpty::Device::Call::ClrSetFeature(pc_io.channel(), 0, fpty::FEATURE_RAW);
  ASSERT_EQ(result6.status(), ZX_OK, "");
  ASSERT_EQ(result6->status, ZX_OK, "");
  ASSERT_EQ(result6->features & fpty::FEATURE_RAW, fpty::FEATURE_RAW, "");
  ASSERT_EQ(write(ps.get(), "\x03", 1), 1, "");
  ASSERT_EQ(read(pc.get(), tmp, 1), 1, "");
  ASSERT_EQ(tmp[0], '\x03', "");
  auto result7 = fpty::Device::Call::ReadEvents(pc_io.channel());
  ASSERT_EQ(result7.status(), ZX_OK, "");
  ASSERT_EQ(result7->status, ZX_OK, "");
  ASSERT_EQ(result7->events, 0u, "");

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
  auto result8 = fpty::Device::Call::MakeActive(pc_io.channel(), n);
  ASSERT_EQ(result8.status(), ZX_OK, "");
  ASSERT_EQ(result8->status, ZX_ERR_NOT_FOUND, "");

  // non-controlling client cannot change active client
  auto result9 = fpty::Device::Call::MakeActive(pc1_io.channel(), n);
  ASSERT_EQ(result9.status(), ZX_OK, "");
  ASSERT_EQ(result9->status, ZX_ERR_ACCESS_DENIED, "");

  // but controlling client can
  n = 1;
  auto result10 = fpty::Device::Call::MakeActive(pc_io.channel(), n);
  ASSERT_EQ(result10.status(), ZX_OK, "");
  ASSERT_EQ(result10->status, ZX_OK, "");
  ASSERT_EQ(fd_signals(pc, 0, zx::time{}), 0, "");
  ASSERT_EQ(fd_signals(pc1, POLLOUT, zx::time{}), POLLOUT, "");
  ASSERT_EQ(write(pc1.get(), "test", 4), 4, "");
  ASSERT_EQ(read(ps.get(), tmp, 4), 4, "");
  ASSERT_EQ(memcmp(tmp, "test", 4), 0, "");

  // make sure controlling client observes departing active client
  pc1_io.reset();
  pc1.reset();
  ASSERT_EQ(fd_signals(pc, POLLHUP | POLLPRI, zx::time::infinite()), POLLHUP | POLLPRI, "");
  auto result11 = fpty::Device::Call::ReadEvents(pc_io.channel());
  ASSERT_EQ(result11.status(), ZX_OK, "");
  ASSERT_EQ(result11->status, ZX_OK, "");
  ASSERT_EQ(result11->events, fpty::EVENT_HANGUP, "");

  // verify that server observes departure of last client
  pc_io.reset();
  pc.reset();
  ASSERT_EQ(fd_signals(ps, POLLHUP | POLLIN, zx::time::infinite()), POLLHUP | POLLIN, "");

  ps_io.reset();
  ps.reset();
}

TEST(PtyTests, not_a_pty_test) {
  fbl::unique_fd root_dir(open("/", O_DIRECTORY | O_RDONLY));
  ASSERT_EQ(bool(root_dir), true, "");

  fdio_cpp::UnownedFdioCaller io(root_dir.get());

  // Sending pty messages such as 'get window size' should fail
  // properly on things that are not ptys.
  auto result = fpty::Device::Call::GetWindowSize(io.channel());
  ASSERT_EQ(result.status(), ZX_ERR_BAD_HANDLE, "");

  io.reset();
}
