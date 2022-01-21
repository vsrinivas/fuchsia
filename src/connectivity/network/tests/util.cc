// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <algorithm>

#include <gtest/gtest.h>

void fill_stream_send_buf(int fd, int peer_fd, ssize_t *out_bytes_written) {
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
    // TODO(https://fxbug.dev/60337): We can use the minimum buffer size once zircon sockets are not
    // artificially increasing the buffer sizes.
    constexpr int bufsize = 64 << 10;
#else
    // We're about to fill the send buffer; shrink it and the other side's receive buffer to the
    // minimum allowed.
    constexpr int bufsize = 1;
#endif

    EXPECT_EQ(setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize)), 0)
        << strerror(errno);
    EXPECT_EQ(setsockopt(peer_fd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize)), 0)
        << strerror(errno);
  }

  int sndbuf_opt;
  socklen_t sndbuf_optlen = sizeof(sndbuf_opt);
  ASSERT_EQ(getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf_opt, &sndbuf_optlen), 0)
      << strerror(errno);
  ASSERT_EQ(sndbuf_optlen, sizeof(sndbuf_opt));

  int rcvbuf_opt;
  socklen_t rcvbuf_optlen = sizeof(rcvbuf_opt);
  ASSERT_EQ(getsockopt(peer_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf_opt, &rcvbuf_optlen), 0)
      << strerror(errno);
  ASSERT_EQ(rcvbuf_optlen, sizeof(rcvbuf_opt));

  ssize_t total_bytes_written = 0;
#if defined(__linux__)
  // If the send buffer is smaller than the receive buffer, the code below will
  // not work because the first write will not be enough to fill the receive
  // buffer.
  ASSERT_GE(sndbuf_opt, rcvbuf_opt);
  // Write enough bytes at once to fill the receive buffer.
  {
    const std::vector<uint8_t> buf(rcvbuf_opt);
    const ssize_t bytes_written = write(fd, buf.data(), buf.size());
    ASSERT_GE(bytes_written, 0u) << strerror(errno);
    ASSERT_EQ(bytes_written, ssize_t(buf.size()));
    total_bytes_written += bytes_written;
  }

  // Wait for the bytes to be available; afterwards the receive buffer will be full.
  while (true) {
    int available_bytes;
    ASSERT_EQ(ioctl(peer_fd, FIONREAD, &available_bytes), 0) << strerror(errno);
    ASSERT_LE(available_bytes, rcvbuf_opt);
    if (available_bytes == rcvbuf_opt) {
      break;
    }
  }

  // Finally the send buffer can be filled with certainty.
  {
    const std::vector<uint8_t> buf(sndbuf_opt);
    const ssize_t bytes_written = write(fd, buf.data(), buf.size());
    ASSERT_GE(bytes_written, 0u) << strerror(errno);
    ASSERT_EQ(bytes_written, ssize_t(buf.size()));
    total_bytes_written += bytes_written;
  }
#else
  // On Fuchsia, it may take a while for a written packet to land in the netstack's send buffer
  // because of the asynchronous copy from the zircon socket to the send buffer. So we use a small
  // timeout which was empirically tested to ensure no flakiness is introduced.
  timeval original_tv;
  socklen_t tv_len = sizeof(original_tv);
  ASSERT_EQ(getsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &original_tv, &tv_len), 0) << strerror(errno);
  ASSERT_EQ(tv_len, sizeof(original_tv));
  const timeval tv = {
      .tv_sec = 0,
      .tv_usec = 1 << 14,  // ~16ms
  };
  ASSERT_EQ(setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)), 0) << strerror(errno);

  const std::vector<uint8_t> buf(sndbuf_opt + rcvbuf_opt);
  // Clocks sometimes jump in infrastructure, which can cause the timeout set above to expire
  // prematurely. Fortunately such jumps are rarely seen in quick succession - if we repeatedly
  // reach the blocking condition we can be reasonably sure that the intended amount of time truly
  // did elapse. Care is taken to reset the counter if data is written, as we are looking for a
  // streak of blocking condition observances.
  for (int i = 0; i < 1 << 6; i++) {
    ssize_t size;
    while ((size = write(fd, buf.data(), buf.size())) > 0) {
      total_bytes_written += size;

      i = 0;
    }
    ASSERT_EQ(size, -1);
    EXPECT_EQ(errno, EAGAIN) << strerror(errno);
  }
  ASSERT_GT(total_bytes_written, 0);
  ASSERT_EQ(setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &original_tv, tv_len), 0) << strerror(errno);
#endif
  if (out_bytes_written != nullptr) {
    *out_bytes_written = total_bytes_written;
  }
}
