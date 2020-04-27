// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>

#include <gtest/gtest.h>

const uint8_t kNotifySuccess = 1;
const uint8_t kNotifyFail = 2;

void NotifySuccess(int ntfyfd) {
  uint8_t c = kNotifySuccess;
  EXPECT_EQ(1, write(ntfyfd, &c, 1));
}

void NotifyFail(int ntfyfd) {
  uint8_t c = kNotifyFail;
  EXPECT_EQ(1, write(ntfyfd, &c, 1));
}

bool WaitSuccess(int ntfyfd, int timeout) {
  struct pollfd fds = {ntfyfd, POLLIN, 0};
  int nfds;
  EXPECT_GE(nfds = poll(&fds, 1, timeout), 0) << strerror(errno);
  if (nfds == 1) {
    uint8_t c = kNotifyFail;
    EXPECT_EQ(1, read(ntfyfd, &c, 1));
    return kNotifySuccess == c;
  } else {
    EXPECT_EQ(1, nfds);
    return false;
  }
}

void StreamAcceptRead(int acptfd, std::string* out, int ntfyfd) {
  int connfd;
  EXPECT_GE(connfd = accept(acptfd, nullptr, nullptr), 0) << strerror(errno);
  if (connfd < 0) {
    NotifyFail(ntfyfd);
    return;
  }

  int n;
  char buf[4096];
  while ((n = read(connfd, buf, sizeof(buf))) > 0) {
    out->append(buf, n);
  }

  EXPECT_EQ(close(connfd), 0) << strerror(errno);
  NotifySuccess(ntfyfd);
}

void StreamConnectRead(struct sockaddr_in* addr, std::string* out, int ntfyfd) {
  int connfd = socket(AF_INET, SOCK_STREAM, 0);
  EXPECT_GE(connfd, 0);
  if (connfd < 0) {
    NotifyFail(ntfyfd);
    return;
  }

  int ret;
  EXPECT_EQ(ret = connect(connfd, (const struct sockaddr*)addr, sizeof(*addr)), 0)
      << strerror(errno);
  if (ret != 0) {
    NotifyFail(ntfyfd);
    return;
  }

  int n;
  char buf[4096];
  while ((n = read(connfd, buf, sizeof(buf))) > 0) {
    out->append(buf, n);
  }

  EXPECT_EQ(close(connfd), 0) << strerror(errno);
  NotifySuccess(ntfyfd);
}

void StreamAcceptWrite(int acptfd, const char* msg, int ntfyfd) {
  int connfd;
  EXPECT_GE(connfd = accept(acptfd, nullptr, nullptr), 0) << strerror(errno);
  if (connfd < 0) {
    NotifyFail(ntfyfd);
    return;
  }

  ASSERT_EQ((ssize_t)strlen(msg), write(connfd, msg, strlen(msg)));

  EXPECT_EQ(close(connfd), 0) << strerror(errno);
  NotifySuccess(ntfyfd);
}

void DatagramRead(int recvfd, std::string* out, struct sockaddr_in* addr, socklen_t* addrlen,
                  int ntfyfd, int timeout) {
  struct pollfd fds = {recvfd, POLLIN, 0};
  int nfds;
  EXPECT_EQ(nfds = poll(&fds, 1, timeout), 1) << strerror(errno);
  if (nfds != 1) {
    NotifyFail(ntfyfd);
    return;
  }

  char buf[4096];
  int nbytes;
  EXPECT_GT(nbytes = recvfrom(recvfd, buf, sizeof(buf), 0, (struct sockaddr*)addr, addrlen), 0)
      << strerror(errno);
  if (nbytes < 0) {
    NotifyFail(ntfyfd);
    return;
  }
  out->append(buf, nbytes);

  NotifySuccess(ntfyfd);
}

void DatagramReadWrite(int recvfd, int ntfyfd) {
  struct pollfd fds = {recvfd, POLLIN, 0};
  int nfds;
  EXPECT_EQ(nfds = poll(&fds, 1, kTimeout), 1) << strerror(errno);
  if (nfds != 1) {
    NotifyFail(ntfyfd);
    return;
  }

  char buf[32];
  struct sockaddr_in peer;
  socklen_t peerlen = sizeof(peer);
  int nbytes;
  EXPECT_GE(nbytes = recvfrom(recvfd, buf, sizeof(buf), 0,
                              reinterpret_cast<struct sockaddr*>(&peer), &peerlen),
            0)
      << strerror(errno);
  ASSERT_EQ(peerlen, sizeof(peer));

  if (nbytes < 0) {
    NotifyFail(ntfyfd);
    return;
  }

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  char addrbuf[INET_ADDRSTRLEN], peerbuf[INET_ADDRSTRLEN];
  const char* addrstr = inet_ntop(addr.sin_family, &addr.sin_addr, addrbuf, sizeof(addrbuf));
  ASSERT_NE(addrstr, nullptr);
  const char* peerstr = inet_ntop(peer.sin_family, &peer.sin_addr, peerbuf, sizeof(peerbuf));
  ASSERT_NE(peerstr, nullptr);
  ASSERT_STREQ(peerstr, addrstr);

  EXPECT_GE(
      nbytes = sendto(recvfd, buf, nbytes, 0, reinterpret_cast<struct sockaddr*>(&peer), peerlen),
      0)
      << strerror(errno);
  if (nbytes < 0) {
    NotifyFail(ntfyfd);
    return;
  }

  NotifySuccess(ntfyfd);
}

void DatagramReadWriteV6(int recvfd, int ntfyfd) {
  struct pollfd fds = {recvfd, POLLIN, 0};
  int nfds;
  EXPECT_EQ(nfds = poll(&fds, 1, kTimeout), 1) << strerror(errno);
  if (nfds != 1) {
    NotifyFail(ntfyfd);
    return;
  }

  char buf[32];
  struct sockaddr_in6 peer;
  socklen_t peerlen = sizeof(peer);
  int nbytes;
  EXPECT_GE(nbytes = recvfrom(recvfd, buf, sizeof(buf), 0,
                              reinterpret_cast<struct sockaddr*>(&peer), &peerlen),
            0)
      << strerror(errno);
  ASSERT_EQ(peerlen, sizeof(peer));
  if (nbytes < 0) {
    NotifyFail(ntfyfd);
    return;
  }

  struct sockaddr_in6 addr = {};
  addr.sin6_family = AF_INET6;
  addr.sin6_addr = IN6ADDR_LOOPBACK_INIT;
  char addrbuf[INET6_ADDRSTRLEN], peerbuf[INET6_ADDRSTRLEN];
  const char* addrstr = inet_ntop(addr.sin6_family, &addr.sin6_addr, addrbuf, sizeof(addrbuf));
  ASSERT_NE(addrstr, nullptr);
  const char* peerstr = inet_ntop(peer.sin6_family, &peer.sin6_addr, peerbuf, sizeof(peerbuf));
  ASSERT_NE(peerstr, nullptr);
  ASSERT_STREQ(peerstr, addrstr);

  EXPECT_GE(
      nbytes = sendto(recvfd, buf, nbytes, 0, reinterpret_cast<struct sockaddr*>(&peer), peerlen),
      0)
      << strerror(errno);
  if (nbytes < 0) {
    NotifyFail(ntfyfd);
    return;
  }

  NotifySuccess(ntfyfd);
}

void fill_stream_send_buf(int fd, int peer_fd) {
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
      .tv_usec = 1 << 16,  // ~65ms
  };
  EXPECT_EQ(setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)), 0) << strerror(errno);
#endif

  // buf size should be neither too small in which case too many writes operation is required
  // to fill out the sending buffer nor too big in which case a big stack is needed for the buf
  // array.
  int cnt = 0;
  {
    char buf[sndbuf_opt + rcvbuf_opt];
    int size;
    while ((size = write(fd, buf, sizeof(buf))) > 0) {
      cnt += size;
    }
  }
  EXPECT_GT(cnt, 0);
  ASSERT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK) << strerror(errno);

#if defined(__linux__)
  EXPECT_EQ(fcntl(fd, F_SETFL, flags), 0) << strerror(errno);
#else
  EXPECT_EQ(setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &original_tv, tv_len), 0) << strerror(errno);
#endif
}
