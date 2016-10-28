// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
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

  fd_set active_readfds, active_writefds, active_exceptfds;
  FD_ZERO(&active_readfds);
  FD_ZERO(&active_writefds);
  FD_ZERO(&active_exceptfds);

  FD_SET(s, &active_readfds);
  int nwatch = s + 1;

  int ret = 0;
  for (;;) {
    fd_set readfds = active_readfds;
    fd_set writefds = active_writefds;
    fd_set exceptfds = active_exceptfds;

    int nready = select(nwatch, &readfds, &writefds, &exceptfds, NULL);
    if (nready < 0) {
      printf("select failed (errno = %d)\n", errno);
      ret = -1;
      goto end;
    }

    if (FD_ISSET(s, &readfds)) {
      --nready;
      // a connection is waiting
      socklen_t addrlen = sizeof(addr);
      int conn = accept(s, (struct sockaddr*)&addr, &addrlen);
      if (conn < 0) {
        close(s);
        printf("accept failed (errno = %d)\n", errno);
        ret = -1;
        goto end;
      }
      char str[INET6_ADDRSTRLEN];
      printf("connected from %s\n",
             sa_to_str((struct sockaddr*)&addr, str, sizeof(str)));

      FD_SET(conn, &active_readfds);
      if (nwatch < (conn + 1))
        nwatch = conn + 1;
    }

    for (int fd = 0; fd < nwatch; fd++) {
      if (nready == 0)
        break;

      if (fd == s)
        continue;

      if (FD_ISSET(fd, &readfds)) {
        --nready;
        // data is ready to read
        int nread;
        char buf[128];
        int done = 0;
        nread = read(fd, buf, sizeof(buf));
        if (nread == 0) {
          done = 1;
        } else if (nread < 0) {
          printf("read failed on fd %d (%d)\n", fd, nread);
          done = 1;
        } else {
          int nwrite = 0;
          while (nwrite < nread) {
            int n = write(fd, buf + nwrite, nread - nwrite);
            if (n < 0) {
              printf("write failed on fd %d (%d)\n", fd, n);
              done = 1;
              break;
            }
            nwrite += n;
          }
        }
        if (done) {
          close(fd);
          FD_CLR(fd, &active_readfds);
        }
      }
    }
  }
 end:
  for (int i = 0; i < nwatch; i++) {
    if (FD_ISSET(i, &active_readfds)) {
      close(i);
    }
  }

  return ret;
}

void usage(void) { printf("usage: selecttest port\n"); }

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return -1;
  }
  return server(argv[1]);
}
