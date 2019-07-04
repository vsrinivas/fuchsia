// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests ensure the zircon libc can talk to netstack.
// No network connection is required, only a running netstack binary.

#include <fuchsia/net/c/fidl.h>
#include <lib/fdio/fd.h>
#include <lib/sync/completion.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <thread>

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
    ASSERT_EQ(ret, 0) << "bind failed: " << strerror(errno)
                      << " port: " << port;

    socklen_t addrlen = sizeof(addr);
    ASSERT_EQ(getsockname(acptfd, (struct sockaddr*)&addr, &addrlen), 0)
        << strerror(errno);
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

  std::vector<std::thread> workers;
  for (int i = 0; i < 10; i++) {
    workers.push_back(std::thread([&handle, &completion]() {
      zx_status_t status = sync_completion_wait(&completion, ZX_TIME_INFINITE);
      ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);

      int16_t out_code;
      status = fuchsia_net_SocketControlClose(handle, &out_code);
      if (status == ZX_OK) {
        EXPECT_EQ(out_code, 0) << strerror(out_code);
      } else {
        EXPECT_EQ(status, ZX_ERR_PEER_CLOSED) << zx_status_get_string(status);
      }
    }));
  }

  sync_completion_signal(&completion);

  std::for_each(workers.begin(), workers.end(),
                std::mem_fn(&std::thread::join));
}
