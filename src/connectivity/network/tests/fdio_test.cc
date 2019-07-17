// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests ensure the zircon libc can talk to netstack.
// No network connection is required, only a running netstack binary.

#include <thread>

#include <fuchsia/posix/socket/cpp/fidl.h>
#include <lib/fdio/fd.h>
#include <lib/sync/completion.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

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
