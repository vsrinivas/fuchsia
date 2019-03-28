// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <poll.h>

#include "gtest/gtest.h"

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
  int nfds = poll(&fds, 1, timeout);
  EXPECT_GE(nfds, 0) << "poll failed: " << errno;
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
  int connfd = accept(acptfd, nullptr, nullptr);
  EXPECT_GE(connfd, 0) << "accept failed: " << errno;
  if (connfd < 0) {
    NotifyFail(ntfyfd);
    return;
  }

  int n;
  char buf[4096];
  while ((n = read(connfd, buf, sizeof(buf))) > 0) {
    out->append(buf, n);
  }

  EXPECT_EQ(0, close(connfd));
  NotifySuccess(ntfyfd);
}

void StreamConnectRead(struct sockaddr_in* addr, std::string* out, int ntfyfd) {
  int connfd = socket(AF_INET, SOCK_STREAM, 0);
  EXPECT_GE(connfd, 0);
  if (connfd < 0) {
    NotifyFail(ntfyfd);
    return;
  }

  int ret = connect(connfd, (const struct sockaddr*)addr, sizeof(*addr));
  EXPECT_EQ(0, ret) << strerror(errno);
  if (ret != 0) {
    NotifyFail(ntfyfd);
    return;
  }

  int n;
  char buf[4096];
  while ((n = read(connfd, buf, sizeof(buf))) > 0) {
    out->append(buf, n);
  }

  EXPECT_EQ(close(connfd), 0);
  NotifySuccess(ntfyfd);
}

void StreamAcceptWrite(int acptfd, const char* msg, int ntfyfd) {
  int connfd = accept(acptfd, nullptr, nullptr);
  EXPECT_GE(connfd, 0) << "accept failed: " << errno;
  if (connfd < 0) {
    NotifyFail(ntfyfd);
    return;
  }

  ASSERT_EQ((ssize_t)strlen(msg), write(connfd, msg, strlen(msg)));

  EXPECT_EQ(0, close(connfd));
  NotifySuccess(ntfyfd);
}

void PollSignal(struct sockaddr_in* addr, short events, short* revents,
                int ntfyfd) {
  int connfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(connfd, 0);

  int ret = connect(connfd, (const struct sockaddr*)addr, sizeof(*addr));
  EXPECT_EQ(0, ret) << "connect failed: " << errno;
  if (ret != 0) {
    NotifyFail(ntfyfd);
    return;
  }

  struct pollfd fds = {connfd, events, 0};

  int n = poll(&fds, 1, kTimeout);
  EXPECT_GT(n, 0) << "poll failed: " << errno;
  if (n <= 0) {
    NotifyFail(ntfyfd);
    return;
  }

  EXPECT_EQ(0, close(connfd));
  *revents = fds.revents;
  NotifySuccess(ntfyfd);
}

void DatagramRead(int recvfd, std::string* out, struct sockaddr_in* addr,
                  socklen_t* addrlen, int ntfyfd, int timeout) {
  struct pollfd fds = {recvfd, POLLIN, 0};
  int nfds = poll(&fds, 1, timeout);
  EXPECT_EQ(1, nfds) << "poll returned: " << nfds << " errno: " << errno;
  if (nfds != 1) {
    NotifyFail(ntfyfd);
    return;
  }

  char buf[4096];
  int nbytes =
      recvfrom(recvfd, buf, sizeof(buf), 0, (struct sockaddr*)addr, addrlen);
  EXPECT_GT(nbytes, 0) << "recvfrom failed: " << errno;
  if (nbytes < 0) {
    NotifyFail(ntfyfd);
    return;
  }
  out->append(buf, nbytes);

  NotifySuccess(ntfyfd);
}

void DatagramReadWrite(int recvfd, int ntfyfd) {
  struct pollfd fds = {recvfd, POLLIN, 0};
  int nfds = poll(&fds, 1, kTimeout);
  EXPECT_EQ(1, nfds) << "poll returned: " << nfds << " errno: " << errno;
  if (nfds != 1) {
    NotifyFail(ntfyfd);
    return;
  }

  char buf[32];
  struct sockaddr_in peer;
  socklen_t peerlen = sizeof(peer);
  int nbytes = recvfrom(recvfd, buf, sizeof(buf), 0,
                        reinterpret_cast<struct sockaddr*>(&peer), &peerlen);
  EXPECT_GE(nbytes, 0) << "recvfrom failed: " << errno;
  if (nbytes < 0) {
    NotifyFail(ntfyfd);
    return;
  }
#if DEBUG
  char addrstr[INET_ADDRSTRLEN];
  printf("peer.sin_addr: %s\n",
         inet_ntop(AF_INET, &peer.sin_addr, addrstr, sizeof(addrstr)));
  printf("peer.sin_port: %d\n", ntohs(peer.sin_port));
  printf("peerlen: %d\n", peerlen);
#endif

  nbytes = sendto(recvfd, buf, nbytes, 0,
                  reinterpret_cast<struct sockaddr*>(&peer), peerlen);
  EXPECT_GE(nbytes, 0) << "sendto failed: " << errno;
  if (nbytes < 0) {
    NotifyFail(ntfyfd);
    return;
  }

  NotifySuccess(ntfyfd);
}

void DatagramReadWriteV6(int recvfd, int ntfyfd) {
  struct pollfd fds = {recvfd, POLLIN, 0};
  int nfds = poll(&fds, 1, kTimeout);
  EXPECT_EQ(1, nfds) << "poll returned: " << nfds << " errno: " << errno;
  if (nfds != 1) {
    NotifyFail(ntfyfd);
    return;
  }

  char buf[32];
  struct sockaddr_in6 peer;
  socklen_t peerlen = sizeof(peer);
  int nbytes = recvfrom(recvfd, buf, sizeof(buf), 0,
                        reinterpret_cast<struct sockaddr*>(&peer), &peerlen);
  EXPECT_GE(nbytes, 0) << "recvfrom failed: " << errno;
  if (nbytes < 0) {
    NotifyFail(ntfyfd);
    return;
  }
#if DEBUG
  char addrstr[INET_ADDRSTRLEN];
  printf("peer.sin6_addr: %s\n",
         inet_ntop(AF_INET6, &peer.sin6_addr, addrstr, sizeof(addrstr)));
  printf("peer.sin6_port: %d\n", ntohs(peer.sin6_port));
  printf("peerlen: %d\n", peerlen);
#endif

  nbytes = sendto(recvfd, buf, nbytes, 0,
                  reinterpret_cast<struct sockaddr*>(&peer), peerlen);
  EXPECT_GE(nbytes, 0) << "sendto failed: " << errno;
  if (nbytes < 0) {
    NotifyFail(ntfyfd);
    return;
  }

  NotifySuccess(ntfyfd);
}
