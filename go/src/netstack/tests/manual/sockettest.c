// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
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

void dump_ai(struct addrinfo* ai) {
  char str[INET6_ADDRSTRLEN];
  printf(
      "family %d, socktype %d, flags %d, protocol %d, addrlen %d, "
      "addr %s\n",
      ai->ai_family, ai->ai_socktype, ai->ai_flags, ai->ai_protocol,
      ai->ai_addrlen,
      ai->ai_addr ? sa_to_str(ai->ai_addr, str, sizeof(str)) : "NULL");
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

  int i = 0;
  for (rp = result; rp != NULL; rp = rp->ai_next) {
    printf("[%d] ", i++);
    dump_ai(rp);
  }

  int s;
  for (rp = result; rp != NULL; rp = rp->ai_next) {
    s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (s < 0) {
      printf("socket failed (errno = %d)\n", errno);
      continue;
    }
    if (connect(s, rp->ai_addr, rp->ai_addrlen) != -1) {
      char str[INET6_ADDRSTRLEN];
      printf("connected to %s\n", sa_to_str(rp->ai_addr, str, sizeof(str)));
      break;
    }
    printf("connect failed (errno = %d)\n", errno);
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

#define NTIMES 4

  for (int i = 0; i < NTIMES; i++) {
    printf("waiting for a connection on port %d...\n", port);
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

    int total_read = 0;
    int total_write = 0;
    for (;;) {
      char buf[128];
      int nread;
      nread = read(conn, buf, sizeof(buf));
      if (nread == 0) {
        // printf("read returned 0\n");
        printf("total: read %d, write %d\n", total_read, total_write);
        break;
      }
      if (nread < 0) {
        printf("read failed (%d)\n", nread);
        close(conn);
        close(s);
        return -1;
      }
      total_read += nread;

      // printf("read success (nread = %d)\n", nread);

      // for (int i = 0; i < nread; i++)
      //     printf("%c", buf[i]);
      // printf("\n");

      int nwrite = 0;
      while (nwrite < nread) {
        int n = write(conn, buf + nwrite, nread - nwrite);
        if (n < 0) {
          printf("write failed (%d)\n", n);
          close(conn);
          close(s);
          return -1;
        }
        nwrite += n;
      }
      total_write += nwrite;
      // printf("write success (nwrite = %d)\n", nwrite);
    }
    close(conn);
  }
  close(s);

  return 0;
}

void usage(void) {
  printf("usage: sockettest server port\n");
  printf("       sockettest client address port message\n");
}

int main(int argc, char** argv) {
  if (argc > 1) {
    if (argv[1][0] == 'c') {  // client
      if (argc > 4) return client(argv[2], argv[3], argv[4]);
    }
    if (argv[1][0] == 's') {  // server
      if (argc > 2) return server(argv[2]);
    }
  }
  usage();
  return -1;
}
