// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <fcntl.h>
#include <sys/socket.h>

#include <gtest/gtest.h>

ssize_t fill_stream_send_buf(int fd, int peer_fd) {
  {
#if defined(__Fuchsia__)
    // In other systems we prefer to get the smallest possible buffer size, but that causes an
    // unnecessarily large amount of writes to fill the send and receive buffers on Fuchsia because
    // of the zircon socket attached to both the sender and the receiver. Each zircon socket will
    // artificially add 256KB (its depth) to the sender's and receiver's buffers.
    //
    // We'll arbitrarily select a larger size which will allow us to fill both zircon sockets
    // faster.
    //
    // TODO(fxbug.dev/60337): We can use the minimum buffer size once zircon sockets are not
    // artificially increasing the buffer sizes.
    constexpr int bufsize = 64 << 10;
#else
    // We're about to fill the send buffer; shrink it and the other side's receive buffer to the
    // minimum allowed.
    constexpr int bufsize = 1;
#endif

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
      .tv_usec = 1 << 13,  // ~8ms
  };
  EXPECT_EQ(setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)), 0) << strerror(errno);
#endif

  ssize_t cnt = 0;
  std::vector<uint8_t> buf(sndbuf_opt + rcvbuf_opt);
  // Clocks sometimes jump in infrastructure, which can cause the timeout set above to expire
  // prematurely. Fortunately such jumps are rarely seen in quick succession - if we repeatedly
  // reach the blocking condition we can be reasonably sure that the intended amount of time truly
  // did elapse. Care is taken to reset the counter if data is written, as we are looking for a
  // streak of blocking condition observances.
  for (int i = 0; i < 1 << 5; i++) {
    ssize_t size;
    while ((size = write(fd, buf.data(), buf.size())) > 0) {
      cnt += size;

      i = 0;
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
