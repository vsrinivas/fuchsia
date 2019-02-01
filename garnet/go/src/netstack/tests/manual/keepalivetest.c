// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Tests TCP keepalives. Use as follows:
// host: run `nc -l 1234`
// test: run `keepalivetest HOST 1234`
// host: run `ifconfig eth0 down` (where eth0) is the interface to the test
// client test should close the socket after 4 failed keepalives (10 + 3*5
// seconds).

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int client(const char* address, const char* service) {
  struct addrinfo hints;
  struct addrinfo *result, *rp;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = 0;
  hints.ai_protocol = 0;

  int r;
  r = getaddrinfo(address, service, &hints, &result);
  if (r != 0) {
    printf("getaddrinfo failed (%d, errno = %d)\n", r, errno);
    return -1;
  }

  int s;
  for (rp = result; rp != NULL; rp = rp->ai_next) {
    s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (s < 0) {
      printf("socket failed (errno = %d)\n", errno);
      continue;
    }
    if (connect(s, rp->ai_addr, rp->ai_addrlen) != -1)
      break;
    printf("connect failed (errno = %d)\n", errno);
    close(s);
  }
  if (rp == NULL) {
    printf("all connect attempts failed\n");
    return -1;
  }
  freeaddrinfo(result);

  int optval = 1;
  if (setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval)) < 0) {
    printf("setsockopt failed: %d\n", errno);
    close(s);
    return -1;
  }
  optval = 10;
  if (setsockopt(s, SOL_TCP, TCP_KEEPIDLE, &optval, sizeof(optval)) < 0) {
    printf("setsockopt failed: %d\n", errno);
    close(s);
    return -1;
  }
  optval = 5;
  if (setsockopt(s, SOL_TCP, TCP_KEEPINTVL, &optval, sizeof(optval)) < 0) {
    printf("setsockopt failed: %d\n", errno);
    close(s);
    return -1;
  }
  optval = 4;
  if (setsockopt(s, SOL_TCP, TCP_KEEPCNT, &optval, sizeof(optval)) < 0) {
    printf("setsockopt failed: %d\n", errno);
    close(s);
    return -1;
  }

  for (;;) {
    char buf[4096];
    int nread = read(s, buf, sizeof(buf));
    if (nread < 0) {
      printf("read failed: %d (errno = %d)\n", nread, errno);
      close(s);
      break;
    }
    printf("read: %s\n", buf);
  }

  close(s);
  return 0;
}

void usage(void) { printf("       keepalivetest address port\n"); }

int main(int argc, char** argv) {
  if (argc > 2)
    return client(argv[1], argv[2]);
  usage();
  return -1;
}
