// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
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

int server(const char* service) {
  int16_t port = atoi(service);
  printf("listen on port %d\n", port);

  int s = socket(AF_INET6, SOCK_STREAM, 0);
  if (s < 0) {
    printf("socket failed (errno = %d)\n", errno);
    return -1;
  }

  struct sockaddr_in6 addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin6_family = AF_INET6;
  addr.sin6_addr = in6addr_any;  // works with IPv4
  addr.sin6_port = htons(port);

  if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    printf("bind failed (errno = %d)\n", errno);
    return -1;
  }

  if (listen(s, 1) < 0) {
    printf("listen failed\n");
    return -1;
  }

  int epfd = epoll_create(0);
  if (epfd < 0) {
    printf("epoll_create failed (errno = %d)\n", errno);
    return -1;
  }

  struct epoll_event event;
  event.data.fd = s;
  event.events = EPOLLIN;
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, s, &event) < 0) {
    printf("epoll_ctl failed (errno = %d)\n", errno);
    return -1;
  }

#define MAXEVENTS 10

  struct epoll_event* events = calloc(MAXEVENTS, sizeof(event));

  printf("listening connections on fd %d\n", s);

  for (;;) {
    int nready = epoll_wait(epfd, events, MAXEVENTS, -1);
    if (nready < 0) {
      printf("epoll_wait failed (errno = %d)\n", errno);
      return -1;
    }

    for (int i = 0; i < nready; i++) {
      if (events[i].events & (EPOLLERR | EPOLLHUP)) {
        printf("error detected on fd %d. closing...\n", events[i].data.fd);
        close(events[i].data.fd);
        continue;
      }

      if (events[i].data.fd == s) {
        // a connection is waiting
        socklen_t addrlen = sizeof(addr);
        int conn = accept(s, (struct sockaddr*)&addr, &addrlen);
        if (conn < 0) {
          close(s);
          printf("accept failed (errno = %d)\n", errno);
          return -1;
        }
        char str[INET6_ADDRSTRLEN];
        printf("connected from %s\n",
               sa_to_str((struct sockaddr*)&addr, str, sizeof(str)));

        event.data.fd = conn;
        event.events = EPOLLIN;
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, conn, &event) < 0) {
          printf("epoll_ctl ADD failed (errno = %d)\n", errno);
          return -1;
        }
      } else {
        // data is ready to read
        int nread;
        char buf[128];
        int done = 0;
        nread = read(events[i].data.fd, buf, sizeof(buf));
        if (nread == 0) {
          done = 1;
        } else if (nread < 0) {
          printf("read failed on fd %d (%d)\n", events[i].data.fd, nread);
          done = 1;
        } else {
          int nwrite = 0;
          while (nwrite < nread) {
            int n = write(events[i].data.fd, buf + nwrite, nread - nwrite);
            if (n < 0) {
              printf("write failed on fd %d (%d)\n", events[i].data.fd, n);
              done = 1;
              break;
            }
            nwrite += n;
          }
        }
        if (done) {
          if (epoll_ctl(epfd, EPOLL_CTL_DEL, events[i].data.fd, NULL) < 0) {
            printf("epoll_ctl DEL failed (errno = %d)\n", errno);
            return -1;
          }
          close(events[i].data.fd);
        }
      }
    }
  }
  close(s);

  return 0;
}

void usage(void) { printf("usage: epolltest port\n"); }

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return -1;
  }
  return server(argv[1]);
}
