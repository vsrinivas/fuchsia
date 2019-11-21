// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests ensure the zircon libc can talk to netstack.
// No network connection is required, only a running netstack binary.

#include <fuchsia/posix/socket/cpp/fidl.h>
#include <lib/fdio/fd.h>
#include <lib/sync/completion.h>
#include <poll.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <thread>

#include <fbl/unique_fd.h>

#include "gtest/gtest.h"
#include "util.h"

TEST(NetStreamTest, BlockingAcceptWriteNoClose) {
  short port = 0;  // will be assigned by the first bind.

  for (int j = 0; j < 2; j++) {
    int acptfd;
    ASSERT_GE(acptfd = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = port;
    addr.sin_addr.s_addr = INADDR_ANY;

    int ret = 0;
    int backoff_msec = 10;
    for (;;) {
      ret = bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr));
      if (j > 0 && ret < 0 && errno == EADDRINUSE) {
        // Wait until netstack detects the peer handle is closed and
        // tears down the port.
        zx_nanosleep(zx_deadline_after(ZX_MSEC(backoff_msec)));
        backoff_msec *= 2;
      } else {
        break;
      }
    }
    ASSERT_EQ(ret, 0) << "bind failed: " << strerror(errno) << " port: " << port;

    socklen_t addrlen = sizeof(addr);
    ASSERT_EQ(getsockname(acptfd, (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);
    ASSERT_EQ(addrlen, sizeof(addr));

    // remember the assigned port and use it for the next bind.
    port = addr.sin_port;

    int ntfyfd[2];
    ASSERT_EQ(pipe(ntfyfd), 0) << strerror(errno);

    ASSERT_EQ(listen(acptfd, 10), 0) << strerror(errno);

    std::string out;
    std::thread thrd(StreamConnectRead, &addr, &out, ntfyfd[1]);

    int connfd;
    ASSERT_GE(connfd = accept(acptfd, nullptr, nullptr), 0) << strerror(errno);

    const char* msg = "hello";
    ASSERT_EQ((ssize_t)strlen(msg), write(connfd, msg, strlen(msg)));
    ASSERT_EQ(close(connfd), 0) << strerror(errno);

    ASSERT_TRUE(WaitSuccess(ntfyfd[0], kTimeout));
    thrd.join();

    EXPECT_STREQ(msg, out.c_str());

    // Simulate unexpected process exit by closing the handle
    // without sending a Close op to netstack.
    zx_handle_t handle;
    zx_status_t status = fdio_fd_transfer(acptfd, &handle);
    ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);
    status = zx_handle_close(handle);
    ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);

    EXPECT_EQ(close(ntfyfd[0]), 0) << strerror(errno);
    EXPECT_EQ(close(ntfyfd[1]), 0) << strerror(errno);
  }
}

TEST(NetStreamTest, RaceClose) {
  int fd;
  ASSERT_GE(fd = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);

  zx_handle_t handle;
  zx_status_t status = fdio_fd_transfer(fd, &handle);
  ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);

  sync_completion_t completion;

  fuchsia::posix::socket::Control_SyncProxy control((zx::channel(handle)));

  std::vector<std::thread> workers;
  for (int i = 0; i < 10; i++) {
    workers.push_back(std::thread([&control, &completion]() {
      zx_status_t status = sync_completion_wait(&completion, ZX_TIME_INFINITE);
      ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);

      zx_status_t io_status = control.Close(&status);
      if (io_status == ZX_OK) {
        EXPECT_EQ(status, ZX_OK) << zx_status_get_string(status);
      } else {
        EXPECT_EQ(io_status, ZX_ERR_PEER_CLOSED) << zx_status_get_string(io_status);
      }
    }));
  }

  sync_completion_signal(&completion);

  std::for_each(workers.begin(), workers.end(), std::mem_fn(&std::thread::join));
}

TEST(SocketTest, ZXSocketSignalNotPermitted) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  zx::channel channel;
  zx_status_t status;
  ASSERT_EQ(status = fdio_fd_transfer(fd.get(), channel.reset_and_get_address()), ZX_OK)
      << zx_status_get_string(status);

  fuchsia::posix::socket::Control_SyncProxy control(std::move(channel));

  fuchsia::io::NodeInfo node_info;
  ASSERT_EQ(status = control.Describe(&node_info), ZX_OK) << zx_status_get_string(status);
  ASSERT_EQ(node_info.Which(), fuchsia::io::NodeInfo::Tag::kSocket);

  zx::socket& socket = node_info.socket().socket;

  EXPECT_EQ(status = socket.signal(ZX_USER_SIGNAL_0, 0), ZX_ERR_ACCESS_DENIED)
      << zx_status_get_string(status);
  EXPECT_EQ(status = socket.signal(0, ZX_USER_SIGNAL_0), ZX_ERR_ACCESS_DENIED)
      << zx_status_get_string(status);
  EXPECT_EQ(status = socket.signal_peer(ZX_USER_SIGNAL_0, 0), ZX_ERR_ACCESS_DENIED)
      << zx_status_get_string(status);
  EXPECT_EQ(status = socket.signal_peer(0, ZX_USER_SIGNAL_0), ZX_ERR_ACCESS_DENIED)
      << zx_status_get_string(status);
}

TEST(SocketTest, CloseZXSocketOnClose) {
  int fd;
  ASSERT_GE(fd = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);

  zx_handle_t handle;
  zx_status_t status;
  ASSERT_EQ(status = fdio_fd_transfer(fd, &handle), ZX_OK) << zx_status_get_string(status);

  fuchsia::posix::socket::Control_SyncProxy control((zx::channel(handle)));

  fuchsia::io::NodeInfo node_info;
  ASSERT_EQ(status = control.Describe(&node_info), ZX_OK) << zx_status_get_string(status);
  ASSERT_EQ(node_info.Which(), fuchsia::io::NodeInfo::Tag::kSocket);

  zx_signals_t observed;
  ASSERT_EQ(status = node_info.socket().socket.wait_one(ZX_SOCKET_WRITABLE,
                                                        zx::time::infinite_past(), &observed),
            ZX_OK)
      << zx_status_get_string(status);
  ASSERT_EQ(status = zx::unowned_channel(handle)->wait_one(ZX_CHANNEL_WRITABLE,
                                                           zx::time::infinite_past(), &observed),
            ZX_OK)
      << zx_status_get_string(status);

  zx_status_t io_status;
  ASSERT_EQ(io_status = control.Close(&status), ZX_OK) << zx_status_get_string(status);
  ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);

  ASSERT_EQ(status = node_info.socket().socket.wait_one(ZX_SOCKET_PEER_CLOSED,
                                                        zx::time::infinite_past(), &observed),
            ZX_OK)
      << zx_status_get_string(status);
  // Give a generous timeout for the channel to close; the channel closing is inherently
  // asynchronous with respect to the `Close` FIDL call above (since its return must come over the
  // channel).
  ASSERT_EQ(status = zx::unowned_channel(handle)->wait_one(
                ZX_CHANNEL_PEER_CLOSED, zx::deadline_after(zx::sec(5)), &observed),
            ZX_OK)
      << zx_status_get_string(status);
}

TEST(SocketTest, AcceptedSocketIsConnected) {
  // Create the listening endpoint (server).
  fbl::unique_fd serverfd;
  ASSERT_TRUE(serverfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  ASSERT_EQ(bind(serverfd.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);
  ASSERT_EQ(listen(serverfd.get(), 1), 0) << strerror(errno);

  // Get the address the server is listening on.
  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(serverfd.get(), reinterpret_cast<struct sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  // Connect to the listening endpoint (client).
  fbl::unique_fd clientfd;
  ASSERT_TRUE(clientfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
  ASSERT_EQ(connect(clientfd.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)),
            0)
      << strerror(errno);

  // Accept the new connection (client) on the listening endpoint (server).
  fbl::unique_fd connfd;
  ASSERT_TRUE(connfd = fbl::unique_fd(accept(serverfd.get(), nullptr, nullptr))) << strerror(errno);
  ASSERT_EQ(close(serverfd.release()), 0) << strerror(errno);

  zx::channel channel;
  zx_status_t status;
  ASSERT_EQ(status = fdio_fd_transfer(connfd.get(), channel.reset_and_get_address()), ZX_OK)
      << zx_status_get_string(status);

  fuchsia::posix::socket::Control_SyncProxy control(std::move(channel));

  fuchsia::io::NodeInfo node_info;
  ASSERT_EQ(status = control.Describe(&node_info), ZX_OK) << zx_status_get_string(status);
  ASSERT_EQ(node_info.Which(), fuchsia::io::NodeInfo::Tag::kSocket);

  zx::socket& socket = node_info.socket().socket;

  zx_signals_t pending;
  ASSERT_EQ(status = socket.wait_one(ZX_USER_SIGNAL_1 | ZX_USER_SIGNAL_3, zx::time::infinite_past(),
                                     &pending),
            ZX_OK)
      << zx_status_get_string(status);
  EXPECT_TRUE(pending & ZX_USER_SIGNAL_1);
  EXPECT_TRUE(pending & ZX_USER_SIGNAL_3);
}

TEST(SocketTest, CloseClonedSocketAfterTcpRst) {
  // Create the listening endpoint (server).
  fbl::unique_fd serverfd;
  ASSERT_TRUE(serverfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  ASSERT_EQ(bind(serverfd.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);
  ASSERT_EQ(listen(serverfd.get(), 1), 0) << strerror(errno);

  // Get the address the server is listening on.
  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(serverfd.get(), reinterpret_cast<struct sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  // Connect to the listening endpoint (client).
  fbl::unique_fd clientfd;
  ASSERT_TRUE(clientfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
  ASSERT_EQ(connect(clientfd.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)),
            0)
      << strerror(errno);

  // Accept the new connection (client) on the listening endpoint (server).
  fbl::unique_fd connfd;
  ASSERT_TRUE(connfd = fbl::unique_fd(accept(serverfd.get(), nullptr, nullptr))) << strerror(errno);
  ASSERT_EQ(close(serverfd.release()), 0) << strerror(errno);

  // Fill up the rcvbuf (client-side).
  fill_stream_send_buf(connfd.get(), clientfd.get());

  // Closing the client-side connection while it has data that has not been
  // read by the client should trigger a TCP RST.
  ASSERT_EQ(close(clientfd.release()), 0) << strerror(errno);

  struct pollfd pfd = {};
  pfd.fd = connfd.get();
  pfd.events = POLLOUT;
  int n = poll(&pfd, 1, kTimeout);
  ASSERT_GE(n, 0) << strerror(errno);
  ASSERT_EQ(n, 1);
  // TODO(crbug.com/1005300): we should check that revents is exactly
  // OUT|ERR|HUP. Currently, this is a bit racey, and we might see OUT and HUP
  // but not ERR due to the hack in socket_server.go which references this same
  // bug.
  ASSERT_TRUE(pfd.revents & (POLLOUT | POLLHUP)) << pfd.revents;

  // Now that the socket's endpoint has been closed, clone the socket (twice
  // to increase the endpoint's reference count to at least 1), then close all
  // copies of the socket.
  zx_status_t status;
  zx::channel channel1, channel2;
  ASSERT_EQ(status = fdio_fd_clone(connfd.get(), channel1.reset_and_get_address()), ZX_OK)
      << zx_status_get_string(status);
  ASSERT_EQ(status = fdio_fd_clone(connfd.get(), channel2.reset_and_get_address()), ZX_OK)
      << zx_status_get_string(status);

  zx_status_t io_status;
  fuchsia::posix::socket::Control_SyncProxy control1(std::move(channel1));
  ASSERT_EQ(io_status = control1.Close(&status), ZX_OK) << zx_status_get_string(io_status);
  ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);

  fuchsia::posix::socket::Control_SyncProxy control2(std::move(channel2));
  ASSERT_EQ(io_status = control2.Close(&status), ZX_OK) << zx_status_get_string(io_status);
  ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);

  ASSERT_EQ(close(connfd.release()), 0) << strerror(errno);
}
