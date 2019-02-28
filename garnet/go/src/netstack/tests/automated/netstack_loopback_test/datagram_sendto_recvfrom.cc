// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <thread>

#include "gtest/gtest.h"

#define DEBUG 0

namespace netstack {

const int32_t kTimeout = 10000;  // 10 seconds

extern void NotifySuccess(int ntfyfd);
extern void NotifyFail(int ntfyfd);
extern bool WaitSuccess(int ntfyfd, int timeout);

// DatagramSendtoRecvfrom tests if UDP send automatically binds an ephemeral
// port where the receiver can responds to.

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

TEST(NetDatagramTest, DatagramSendtoRecvfrom) {
  int recvfd = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_GE(recvfd, 0);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = 0;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  int ret = bind(recvfd, reinterpret_cast<const struct sockaddr*>(&addr),
                 sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << errno;

  socklen_t addrlen = sizeof(addr);
  ret =
      getsockname(recvfd, reinterpret_cast<struct sockaddr*>(&addr), &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << errno;
#if DEBUG
  char addrstr[INET_ADDRSTRLEN];
  printf("addr.sin_addr: %s\n",
         inet_ntop(AF_INET, &addr.sin_addr, addrstr, sizeof(addrstr)));
  printf("addr.sin_port: %d\n", ntohs(addr.sin_port));
  printf("addrlen: %d\n", addrlen);
#endif

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  std::thread thrd(DatagramReadWrite, recvfd, ntfyfd[1]);

  const char* msg = "hello";

  int sendfd = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_GE(sendfd, 0) << "socket failed: " << errno;
  ASSERT_EQ((ssize_t)strlen(msg),
            sendto(sendfd, msg, strlen(msg), 0,
                   reinterpret_cast<struct sockaddr*>(&addr), addrlen))
      << "sendto failed: " << errno;

  struct pollfd fds = {sendfd, POLLIN, 0};
  int nfds = poll(&fds, 1, kTimeout);
  ASSERT_EQ(1, nfds) << "poll returned: " << nfds << " errno: " << errno;

  char buf[32];
  struct sockaddr_in peer;
  socklen_t peerlen = sizeof(peer);
  ASSERT_EQ((ssize_t)strlen(msg),
            recvfrom(sendfd, buf, sizeof(buf), 0,
                     reinterpret_cast<struct sockaddr*>(&peer), &peerlen))
      << "recvfrom failed: " << errno;
#if DEBUG
  printf("peer.sin_addr[2]: %s\n",
         inet_ntop(AF_INET, &peer.sin_addr, addrstr, sizeof(addrstr)));
  printf("peer.sin_port[2]: %d\n", ntohs(peer.sin_port));
  printf("peerlen[2]: %d\n", peerlen);
#endif

  ASSERT_EQ(0, close(sendfd));

  ASSERT_EQ(true, WaitSuccess(ntfyfd[0], kTimeout));
  thrd.join();

  EXPECT_EQ(0, close(recvfd));
  EXPECT_EQ(0, close(ntfyfd[0]));
  EXPECT_EQ(0, close(ntfyfd[1]));
}

// DatagramSendtoRecvfromV6 tests if UDP send automatically binds an ephemeral
// port where the receiver can responds to.

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

TEST(NetDatagramTest, DatagramSendtoRecvfromV6) {
  int recvfd = socket(AF_INET6, SOCK_DGRAM, 0);
  ASSERT_GE(recvfd, 0);

  struct sockaddr_in6 addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin6_family = AF_INET6;
  addr.sin6_port = 0;
  addr.sin6_addr = in6addr_loopback;

  int ret = bind(recvfd, reinterpret_cast<const struct sockaddr*>(&addr),
                 sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << errno;

  socklen_t addrlen = sizeof(addr);
  ret =
      getsockname(recvfd, reinterpret_cast<struct sockaddr*>(&addr), &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << errno;
#if DEBUG
  char addrstr[INET_ADDRSTRLEN];
  printf("addr.sin6_addr: %s\n",
         inet_ntop(AF_INET6, &addr.sin6_addr, addrstr, sizeof(addrstr)));
  printf("addr.sin6_port: %d\n", ntohs(addr.sin6_port));
  printf("addrlen: %d\n", addrlen);
#endif

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  std::thread thrd(DatagramReadWriteV6, recvfd, ntfyfd[1]);

  const char* msg = "hello";

  int sendfd = socket(AF_INET6, SOCK_DGRAM, 0);
  ASSERT_GE(sendfd, 0) << "socket failed: " << errno;
  ASSERT_EQ((ssize_t)strlen(msg),
            sendto(sendfd, msg, strlen(msg), 0,
                   reinterpret_cast<struct sockaddr*>(&addr), addrlen))
      << "sendto failed: " << errno;

  struct pollfd fds = {sendfd, POLLIN, 0};
  int nfds = poll(&fds, 1, kTimeout);
  ASSERT_EQ(1, nfds) << "poll returned: " << nfds << " errno: " << errno;

  char buf[32];
  struct sockaddr_in6 peer;
  socklen_t peerlen = sizeof(peer);
  ASSERT_EQ((ssize_t)strlen(msg),
            recvfrom(sendfd, buf, sizeof(buf), 0,
                     reinterpret_cast<struct sockaddr*>(&peer), &peerlen))
      << "recvfrom failed: " << errno;
#if DEBUG
  printf("peer.sin6_addr[2]: %s\n",
         inet_ntop(AF_INET6, &peer.sin6_addr, addrstr, sizeof(addrstr)));
  printf("peer.sin6_port[2]: %d\n", ntohs(peer.sin6_port));
  printf("peerlen[2]: %d\n", peerlen);
#endif

  ASSERT_EQ(0, close(sendfd));

  ASSERT_EQ(true, WaitSuccess(ntfyfd[0], kTimeout));
  thrd.join();

  EXPECT_EQ(0, close(recvfd));
  EXPECT_EQ(0, close(ntfyfd[0]));
  EXPECT_EQ(0, close(ntfyfd[1]));
}

}  // namespace netstack
