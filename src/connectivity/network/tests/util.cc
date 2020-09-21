// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <fcntl.h>
#include <sys/socket.h>

#include <gtest/gtest.h>

ssize_t fill_stream_send_buf(int fd, int peer_fd) {
  // We're about to fill the send buffer; shrink it and the other side's receive buffer to the
  // minimum allowed.
  {
    const int bufsize = 1;
    socklen_t optlen = sizeof(bufsize);

    EXPECT_EQ(setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufsize, optlen), 0) << strerror(errno);
    EXPECT_EQ(setsockopt(peer_fd, SOL_SOCKET, SO_RCVBUF, &bufsize, optlen), 0) << strerror(errno);
  }

  int sndbuf_opt;
  socklen_t sndbuf_optlen = sizeof(sndbuf_opt);
  EXPECT_EQ(getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf_opt, &sndbuf_optlen), 0)
      << strerror(errno);
  EXPECT_EQ(sndbuf_optlen, sizeof(sndbuf_opt));

  int rcvbuf_opt;
  socklen_t rcvbuf_optlen = sizeof(rcvbuf_opt);
  EXPECT_EQ(getsockopt(peer_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf_opt, &rcvbuf_optlen), 0)
      << strerror(errno);
  EXPECT_EQ(rcvbuf_optlen, sizeof(rcvbuf_opt));

  // Now that the buffers involved are minimal, we can temporarily make the socket non-blocking on
  // Linux without introducing flakiness. We can't do that on Fuchsia because of the asynchronous
  // copy from the zircon socket to the "real" send buffer, which takes a bit of time, so we use
  // a small timeout which was empirically tested to ensure no flakiness is introduced.
#if defined(__linux__)
  int flags;
  EXPECT_GE(flags = fcntl(fd, F_GETFL), 0) << strerror(errno);
  EXPECT_EQ(fcntl(fd, F_SETFL, flags | O_NONBLOCK), 0) << strerror(errno);
#else
  struct timeval original_tv;
  socklen_t tv_len = sizeof(original_tv);
  EXPECT_EQ(getsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &original_tv, &tv_len), 0) << strerror(errno);
  EXPECT_EQ(tv_len, sizeof(original_tv));
  const struct timeval tv = {
      .tv_sec = 0,
      .tv_usec = 1 << 18,  // ~262ms
  };
  EXPECT_EQ(setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)), 0) << strerror(errno);
#endif

  // buf size should be neither too small in which case too many writes operation is required
  // to fill out the sending buffer nor too big in which case a big stack is needed for the buf
  // array.
  ssize_t cnt = 0;
  {
    char buf[sndbuf_opt + rcvbuf_opt];
    ssize_t size;
    while ((size = write(fd, buf, sizeof(buf))) > 0) {
      cnt += size;
    }
    EXPECT_EQ(size, -1);
    EXPECT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK) << strerror(errno);
  }
  EXPECT_GT(cnt, 0);
#if defined(__linux__)
  EXPECT_EQ(fcntl(fd, F_SETFL, flags), 0) << strerror(errno);
#else
  EXPECT_EQ(setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &original_tv, tv_len), 0) << strerror(errno);
#endif
  return cnt;
}
