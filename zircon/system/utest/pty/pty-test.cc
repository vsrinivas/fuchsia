// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.hardware.pty/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/io.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/time.h>
#include <poll.h>
#include <unistd.h>
#include <zircon/status.h>

#include <zxtest/zxtest.h>

namespace fpty = fuchsia_hardware_pty;

zx::status<uint32_t> fd_signals(const fbl::unique_fd& fd, uint32_t wait_for_any,
                                zx::time deadline) {
  uint32_t signals = 0;
  zx_status_t status = fdio_wait_fd(fd.get(), wait_for_any, &signals, deadline.get());
  if (status != ZX_OK && status != ZX_ERR_TIMED_OUT) {
    return zx::error(status);
  }
  return zx::ok(signals);
}

#define ASSERT_SIGNALS(val1, val2) \
  ASSERT_OK(val1);                 \
  ASSERT_EQ(val1.value(), val2)

static ssize_t write_full(const fbl::unique_fd& fd) {
  char tmp[300];
  memset(tmp, 0x33, sizeof(tmp));
  ssize_t total = 0;
  for (;;) {
    ssize_t r = write(fd.get(), tmp, sizeof(tmp));
    if (r < 0) {
      if (errno == EAGAIN) {
        break;
      }
      return r;
    }
    if (r == 0) {
      break;
    }
    total += r;
  }
  return total;
}

static ssize_t read_all(const fbl::unique_fd& fd) {
  char tmp[700];
  ssize_t total = 0;
  for (;;) {
    ssize_t r = read(fd.get(), tmp, sizeof(tmp));
    if (r < 0) {
      if (errno == EAGAIN) {
        break;
      }
      return r;
    }
    if (r == 0) {
      break;
    }
    for (ssize_t n = 0; n < r; n++) {
      if (tmp[n] != 0x33) {
        return -EFAULT;
      }
    }
    total += r;
  }
  return total;
}

static zx_status_t open_client(const fbl::unique_fd& fd, uint32_t client_id, int* out_fd) {
  if (!out_fd) {
    return ZX_ERR_INVALID_ARGS;
  }

  fdio_cpp::UnownedFdioCaller io(fd.get());

  zx::status endpoints = fidl::CreateEndpoints<fpty::Device>();
  if (endpoints.is_error()) {
    return endpoints.status_value();
  }

  auto result = fidl::WireCall(io.borrow_as<fpty::Device>())
                    ->OpenClient(client_id, std::move(endpoints->server));

  if (result.status() != ZX_OK) {
    return result.status();
  }
  if (result.value().s != ZX_OK) {
    return result.value().s;
  }

  zx_status_t status = fdio_fd_create(endpoints->client.channel().release(), out_fd);
  if (status != ZX_OK) {
    return status;
  }
  return fcntl(*out_fd, F_SETFL, O_NONBLOCK);
}

TEST(PtyTests, pty_test) {
  // Connect to the PTY service.  We have to do this dance rather than just
  // using open() because open() uses the DESCRIBE flag internally, and the
  // plumbing of the PTY service through svchost causes the DESCRIBE to get
  // consumed by the wrong code, resulting in the wrong NodeInfoDeprecated being provided.
  // This manifests as a loss of fd signals.
  fbl::unique_fd ps;
  {
    zx::status client_end = component::Connect<fpty::Device>();
    ASSERT_OK(client_end.status_value());

    ASSERT_OK(fdio_fd_create(client_end->channel().release(), ps.reset_and_get_address()));
    ASSERT_TRUE(ps.is_valid());
    int flags;
    ASSERT_GE(flags = fcntl(ps.get(), F_GETFL), 0, "%s", strerror(errno));
    ASSERT_EQ(fcntl(ps.get(), F_SETFL, flags | O_NONBLOCK), 0, "%s", strerror(errno));
  }

  fdio_cpp::UnownedFdioCaller ps_io(ps.get());

  fbl::unique_fd pc;
  ASSERT_OK(open_client(ps, 0, pc.reset_and_get_address()));
  ASSERT_TRUE(pc.is_valid());

  fdio_cpp::UnownedFdioCaller pc_io(pc.get());

  char tmp[32];

  ASSERT_SIGNALS(fd_signals(ps, POLLOUT, zx::time{}), POLLOUT);
  ASSERT_SIGNALS(fd_signals(pc, POLLOUT, zx::time{}), POLLOUT);

  // nothing to read
  ASSERT_EQ(read(ps.get(), tmp, 32), -1);
  ASSERT_EQ(errno, EAGAIN, "%s", strerror(errno));
  ASSERT_EQ(read(pc.get(), tmp, 32), -1);
  ASSERT_EQ(errno, EAGAIN, "%s", strerror(errno));

  // write server, read client
  ASSERT_EQ(write(ps.get(), "xyzzy", 5), 5, "%s", strerror(errno));
  ASSERT_SIGNALS(fd_signals(pc, POLLIN | POLLOUT, zx::time{}), POLLIN | POLLOUT);

  memset(tmp, 0, 6);
  ASSERT_EQ(read(pc.get(), tmp, 5), 5, "%s", strerror(errno));
  ASSERT_SUBSTR(tmp, "xyzzy");
  ASSERT_SIGNALS(fd_signals(pc, POLLOUT, zx::time{}), POLLOUT);

  // write client, read server
  ASSERT_EQ(write(pc.get(), "xyzzy", 5), 5, "%s", strerror(errno));
  ASSERT_SIGNALS(fd_signals(ps, POLLIN | POLLOUT, zx::time{}), POLLIN | POLLOUT);

  memset(tmp, 0, 6);
  ASSERT_EQ(read(ps.get(), tmp, 5), 5, "%s", strerror(errno));
  ASSERT_SUBSTR(tmp, "xyzzy");
  ASSERT_SIGNALS(fd_signals(ps, POLLOUT, zx::time{}), POLLOUT);

  // write server until full, then drain
  ASSERT_EQ(write_full(ps), 4096, "%s", strerror(errno));
  ASSERT_SIGNALS(fd_signals(ps, 0, zx::time{}), 0);
  ASSERT_EQ(read_all(pc), 4096, "%s", strerror(errno));
  ASSERT_SIGNALS(fd_signals(ps, POLLOUT, zx::time{}), POLLOUT);

  // write client until full, then drain
  ASSERT_EQ(write_full(pc), 4096, "%s", strerror(errno));
  ASSERT_SIGNALS(fd_signals(pc, 0, zx::time{}), 0);
  ASSERT_EQ(read_all(ps), 4096, "%s", strerror(errno));
  ASSERT_SIGNALS(fd_signals(pc, POLLOUT, zx::time{}), POLLOUT);

  // verify no events pending
  auto result1 = fidl::WireCall(pc_io.borrow_as<fpty::Device>())->ReadEvents();

  ASSERT_OK(result1.status());
  ASSERT_OK(result1.value().status);
  ASSERT_EQ(result1.value().events, 0u);

  // write a ctrl-c
  ASSERT_EQ(write(ps.get(), "\x03", 1), 1, "%s", strerror(errno));

  // should be an event now
  auto result2 = fidl::WireCall(pc_io.borrow_as<fpty::Device>())->ReadEvents();
  ASSERT_OK(result2.status());
  ASSERT_OK(result2.value().status);
  ASSERT_EQ(result2.value().events, fpty::wire::kEventInterrupt);

  // should vanish once we read it
  auto result3 = fidl::WireCall(pc_io.borrow_as<fpty::Device>())->ReadEvents();
  ASSERT_OK(result3.status());
  ASSERT_OK(result3.value().status);
  ASSERT_EQ(result3.value().events, 0u);

  // write something containing a special char
  // should write up to and including the special char
  // converting the special char to a signal
  ASSERT_EQ(write(ps.get(), "hello\x03world", 11), 6, "%s", strerror(errno));
  ASSERT_EQ(read(pc.get(), tmp, 6), 5, "%s", strerror(errno));
  ASSERT_SUBSTR(tmp, "hello");
  auto result4 = fidl::WireCall(pc_io.borrow_as<fpty::Device>())->ReadEvents();
  ASSERT_OK(result4.status());
  ASSERT_OK(result4.value().status);
  ASSERT_EQ(result4.value().events, fpty::wire::kEventInterrupt);

  auto ws_result1 = fidl::WireCall(pc_io.borrow_as<fpty::Device>())->GetWindowSize();
  ASSERT_OK(ws_result1.status());
  ASSERT_OK(ws_result1.value().status);
  ASSERT_EQ(ws_result1.value().size.width, 0u, "%s", strerror(errno));
  ASSERT_EQ(ws_result1.value().size.height, 0u, "%s", strerror(errno));

  fpty::wire::WindowSize ws;
  ws.width = 80;
  ws.height = 25;
  auto result5 = fidl::WireCall(pc_io.borrow_as<fpty::Device>())->SetWindowSize(ws);
  ASSERT_OK(result5.status());
  ASSERT_OK(result5.value().status);
  auto ws_result2 = fidl::WireCall(pc_io.borrow_as<fpty::Device>())->GetWindowSize();
  ASSERT_OK(ws_result2.status());
  ASSERT_OK(ws_result2.value().status);
  ASSERT_EQ(ws_result2.value().size.width, 80u, "%s", strerror(errno));
  ASSERT_EQ(ws_result2.value().size.height, 25u, "%s", strerror(errno));
  auto ws_result3 = fidl::WireCall(pc_io.borrow_as<fpty::Device>())->ReadEvents();
  ASSERT_OK(ws_result3.status());
  ASSERT_OK(ws_result3.value().status);
  ASSERT_EQ(ws_result3.value().events, fpty::wire::kEventWindowSize);

  // verify that we don't get events for special chars in raw mode
  auto result6 =
      fidl::WireCall(pc_io.borrow_as<fpty::Device>())->ClrSetFeature(0, fpty::wire::kFeatureRaw);
  ASSERT_OK(result6.status());
  ASSERT_OK(result6.value().status);
  ASSERT_EQ(result6.value().features & fpty::wire::kFeatureRaw, fpty::wire::kFeatureRaw);
  ASSERT_EQ(write(ps.get(), "\x03", 1), 1, "%s", strerror(errno));
  ASSERT_EQ(read(pc.get(), tmp, 1), 1, "%s", strerror(errno));
  ASSERT_EQ(tmp[0], '\x03', "%s", strerror(errno));
  auto result7 = fidl::WireCall(pc_io.borrow_as<fpty::Device>())->ReadEvents();
  ASSERT_OK(result7.status());
  ASSERT_OK(result7.value().status);
  ASSERT_EQ(result7.value().events, 0u);

  // create a second client
  fbl::unique_fd pc1;
  ASSERT_OK(open_client(pc, 1, pc1.reset_and_get_address()));
  ASSERT_TRUE(pc1.is_valid());

  fdio_cpp::UnownedFdioCaller pc1_io(pc1.get());

  // reads/writes to non-active client should block
  ASSERT_SIGNALS(fd_signals(pc1, 0, zx::time{}), 0);
  ASSERT_EQ(write(pc1.get(), "test", 4), -1);
  ASSERT_EQ(errno, EAGAIN, "%s", strerror(errno));
  ASSERT_EQ(read(pc1.get(), tmp, 4), -1);
  ASSERT_EQ(errno, EAGAIN, "%s", strerror(errno));

  uint32_t n = 2;
  auto result8 = fidl::WireCall(pc_io.borrow_as<fpty::Device>())->MakeActive(n);
  ASSERT_EQ(result8.status(), ZX_OK);
  ASSERT_STATUS(result8.value().status, ZX_ERR_NOT_FOUND);

  // non-controlling client cannot change active client
  auto result9 = fidl::WireCall(pc1_io.borrow_as<fpty::Device>())->MakeActive(n);
  ASSERT_EQ(result9.status(), ZX_OK);
  ASSERT_STATUS(result9.value().status, ZX_ERR_ACCESS_DENIED);

  // but controlling client can
  n = 1;
  auto result10 = fidl::WireCall(pc_io.borrow_as<fpty::Device>())->MakeActive(n);
  ASSERT_OK(result10.status());
  ASSERT_OK(result10.value().status);
  ASSERT_SIGNALS(fd_signals(pc, 0, zx::time{}), 0);
  ASSERT_SIGNALS(fd_signals(pc1, POLLOUT, zx::time{}), POLLOUT);
  ASSERT_EQ(write(pc1.get(), "test", 4), 4, "%s", strerror(errno));
  ASSERT_EQ(read(ps.get(), tmp, 4), 4, "%s", strerror(errno));
  ASSERT_SUBSTR(tmp, "test");

  // make sure controlling client observes departing active client
  pc1_io.reset();
  pc1.reset();
  ASSERT_SIGNALS(fd_signals(pc, POLLHUP | POLLPRI, zx::time::infinite()), POLLHUP | POLLPRI);
  auto result11 = fidl::WireCall(pc_io.borrow_as<fpty::Device>())->ReadEvents();
  ASSERT_OK(result11.status());
  ASSERT_OK(result11.value().status);
  ASSERT_EQ(result11.value().events, fpty::wire::kEventHangup);

  // verify that server observes departure of last client
  pc_io.reset();
  pc.reset();
  ASSERT_SIGNALS(fd_signals(ps, POLLHUP | POLLIN, zx::time::infinite()), POLLHUP | POLLIN);

  ps_io.reset();
  ps.reset();
}

TEST(PtyTests, not_a_pty_test) {
  fbl::unique_fd root_dir;
  ASSERT_TRUE(root_dir = fbl::unique_fd(open("/", O_DIRECTORY | O_RDONLY)), "%s", strerror(errno));

  fdio_cpp::UnownedFdioCaller io(root_dir.get());

  // Sending pty messages such as 'get window size' should fail
  // properly on things that are not ptys.
  auto result = fidl::WireCall(io.borrow_as<fpty::Device>())->GetWindowSize();
  ASSERT_STATUS(result.status(), ZX_ERR_BAD_HANDLE);

  io.reset();
}
