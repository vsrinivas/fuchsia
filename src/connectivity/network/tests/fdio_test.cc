// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests ensure the zircon libc can talk to netstack.
// No network connection is required, only a running netstack binary.

#include <fuchsia/net/c/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/sync/completion.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <thread>

#include "gtest/gtest.h"
#include "util.h"

zx_handle_t GetHandle(int fd) {
  fdio_t* io;
  zx_status_t status = fdio_unbind_from_fd(fd, &io);
  EXPECT_EQ(status, ZX_OK) << zx_status_get_string(status);
  zx_handle_t h;
  zx_signals_t sigs;
  fdio_unsafe_wait_begin(io, 0, &h, &sigs);
  EXPECT_NE(h, ZX_HANDLE_INVALID);
  fdio_unsafe_release(io);
  return h;
}

TEST(NetStreamTest, BlockingAcceptWriteNoClose) {
  short port = 0;  // will be assigned by the first bind.

  for (int j = 0; j < 2; j++) {
    int acptfd = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(acptfd, 0);

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
    ASSERT_EQ(0, ret) << "bind failed: " << errno << " port: " << port;

    socklen_t addrlen = sizeof(addr);
    ret = getsockname(acptfd, (struct sockaddr*)&addr, &addrlen);
    ASSERT_EQ(0, ret) << "getsockname failed: " << errno;

    // remember the assigned port and use it for the next bind.
    port = addr.sin_port;

    int ntfyfd[2];
    ASSERT_EQ(0, pipe(ntfyfd));

    ret = listen(acptfd, 10);
    ASSERT_EQ(0, ret) << "listen failed: " << errno;

    std::string out;
    std::thread thrd(StreamConnectRead, &addr, &out, ntfyfd[1]);

    int connfd = accept(acptfd, nullptr, nullptr);
    ASSERT_GE(connfd, 0) << "accept failed: " << errno;

    const char* msg = "hello";
    ASSERT_EQ((ssize_t)strlen(msg), write(connfd, msg, strlen(msg)));
    ASSERT_EQ(0, close(connfd));

    ASSERT_EQ(true, WaitSuccess(ntfyfd[0], kTimeout));
    thrd.join();

    EXPECT_STREQ(msg, out.c_str());

    // Simulate unexpected process exit by closing the socket handle
    // without sending a Close op to netstack.
    zx_handle_close(GetHandle(acptfd));

    EXPECT_EQ(0, close(ntfyfd[0]));
    EXPECT_EQ(0, close(ntfyfd[1]));
  }
}

TEST(NetStreamTest, RaceClose) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(fd, 0) << strerror(errno);

  zx_handle_t h = GetHandle(fd);

  sync_completion_t completion;

  std::vector<std::thread> workers;
  for (int i = 0; i < 10; i++) {
    workers.push_back(std::thread([&h, &completion]() {
      ASSERT_EQ(sync_completion_wait(&completion, ZX_TIME_INFINITE), ZX_OK);

      int16_t out_code;
      zx_status_t status = fuchsia_net_SocketControlClose(h, &out_code);
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
