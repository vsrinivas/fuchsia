// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <errno.h>
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
    close(s);
    return -1;
  }

  if (listen(s, 1) < 0) {
    printf("listen failed\n");
    close(s);
    return -1;
  }

#define MAXEVENTS 10

  struct pollfd events[MAXEVENTS];

  events[0].fd = s;
  events[0].events = POLLIN;
  nfds_t nfds = 1;

  int ret = 0;
  for (;;) {
    int nready = poll(events, nfds, -1);
    if (nready < 0) {
      printf("poll failed (errno = %d)\n", errno);
      ret = -1;
      goto end;
    }

    for (nfds_t i = 0; i < nfds; i++) {
      if (nready == 0) {
        break;
      }
      if (events[i].revents == 0) {
        continue;
      }
      --nready;

      if (events[i].revents & (POLLERR | POLLHUP)) {
        printf("error detected on fd %d. closing...\n", events[i].fd);
        close(events[i].fd);
        continue;
      }

      if (events[i].fd == s) {
        // a connection is waiting
        socklen_t addrlen = sizeof(addr);
        int conn = accept(s, (struct sockaddr*)&addr, &addrlen);
        if (conn < 0) {
          printf("accept failed (errno = %d)\n", errno);
          ret = -1;
          goto end;
        }
        char str[INET6_ADDRSTRLEN];
        printf("connected from %s\n",
               sa_to_str((struct sockaddr*)&addr, str, sizeof(str)));

        if (nfds <= MAXEVENTS) {
          events[nfds].fd = conn;
          events[nfds].events = POLLIN;
          nfds++;
        } else {
          printf("too many connections. closing\n");
          close(conn);
        }
      } else {
        // data is ready to read
        int nread;
        char buf[128];
        int done = 0;
        nread = read(events[i].fd, buf, sizeof(buf));
        if (nread == 0) {
          done = 1;
        } else if (nread < 0) {
          printf("read failed on fd %d (%d)\n", events[i].fd, nread);
          done = 1;
        } else {
          int nwrite = 0;
          while (nwrite < nread) {
            int n = write(events[i].fd, buf + nwrite, nread - nwrite);
            if (n < 0) {
              printf("write failed on fd %d (%d)\n", events[i].fd, n);
              done = 1;
              break;
            }
            nwrite += n;
          }
        }
        if (done) {
          close(events[i].fd);
          events[i].fd = -1;
        }
      }
    }
  }

 end:
  for (nfds_t i = 0; i < nfds; i++) {
    if (events[i].fd >= 0)
      close(events[i].fd);
  }

  return ret;
}

void usage(void) { printf("usage: polltest port\n"); }

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return -1;
  }
  return server(argv[1]);
}
