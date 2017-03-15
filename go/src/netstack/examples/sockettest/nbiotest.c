// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

const char* sa_to_str(const struct sockaddr* sa, char* str, size_t strlen) {
  if (sa->sa_family == AF_INET) {
    struct sockaddr_in* sin = (struct sockaddr_in*)sa;
    return inet_ntop(AF_INET, &sin->sin_addr, str, strlen);
  } else if (sa->sa_family == AF_INET6) {
    struct sockaddr_in6* sin6 = (struct sockaddr_in6*)sa;
    return inet_ntop(AF_INET6, &sin6->sin6_addr, str, strlen);
  } else {
    return NULL;
  }
}

int client(const char* address, const char* service, const char* message) {
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

    int status = fcntl(s, F_GETFL, 0);
    r = fcntl(s, F_SETFL, status | O_NONBLOCK);
    if (r != 0) {
      printf("fctl set O_NONBLOCK failed (%d, errno = %d)\n", r, errno);
    }

    if (connect(s, rp->ai_addr, rp->ai_addrlen) != -1) {
      char str[INET6_ADDRSTRLEN];
      printf("connected to %s\n", sa_to_str(rp->ai_addr, str, sizeof(str)));
      break;
    }

    if (errno == EINPROGRESS) {
      printf("connect in progress...\n");

      struct pollfd pfd = {s, POLLOUT, 0};
      r = poll(&pfd, 1, -1);
      if (r < 0) {
        printf("poll failed (%d, errno = %d)\n", r, errno);
      }

      int val;
      socklen_t vallen = sizeof(val);
      r = getsockopt(s, SOL_SOCKET, SO_ERROR, &val, &vallen);
      if (r != 0) {
        printf("getsockopt failed (errno = %d)\n", errno);
      } else if (vallen != sizeof(val)) {
        printf("getsockopt: vallen %d != sizeof(val)\n", vallen);
      } else if (val != 0) {
        printf("getsockopt: val %d != 0\n", val);
      } else {
        char str[INET6_ADDRSTRLEN];
        printf("connected to %s\n", sa_to_str(rp->ai_addr, str, sizeof(str)));
        break;
      }
    } else {
      printf("connect failed (errno = %d)\n", errno);
    }
    close(s);
  }
  if (rp == NULL) {
    printf("all connect attempts failed\n");
    return -1;
  }
  freeaddrinfo(result);

  char buf[128];
  if (strlen(message) + 1 > sizeof(buf)) {
    printf("message is too long\n");
    return -1;
  }
  strcpy(buf, message);
  strcat(buf, "\n");

  int nwrite;
  nwrite = write(s, buf, strlen(buf));
  if (nwrite < 0) {
    printf("write failed (%d)\n", nwrite);
    close(s);
    return -1;
  }
  printf("write success (nwrite = %d)\n", nwrite);

  close(s);
  return 0;
}

void usage(void) { printf("usage: nbiotest address port message\n"); }

int main(int argc, char** argv) {
  if (argc != 4) {
    usage();
    return -1;
  }
  return client(argv[1], argv[2], argv[3]);
}
