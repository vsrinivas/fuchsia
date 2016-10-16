// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

void dump_addr_in(struct sockaddr_in* addr) {
  printf("0x%08x\n", addr->sin_addr.s_addr);
}

void dump_ai(struct addrinfo* ai) {
  printf("family = %d", ai->ai_family);
  printf(", socktype = %d", ai->ai_socktype);
  printf(", flags = 0x%x", ai->ai_flags);
  printf(", protocol = %d", ai->ai_protocol);
  printf(", addrlen = %d", ai->ai_addrlen);
  if (ai->ai_addr != NULL) {
    // TODO: could be IPv6
    printf(", addr = ");
    dump_addr_in((struct sockaddr_in*)ai->ai_addr);

  } else {
    printf(", addr = NULL\n");
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

  // printf("hints: "); dump_ai(&hints);

  int r;
  r = getaddrinfo(address, service, &hints, &result);
  if (r != 0) {
    printf("getaddrinfo failed (%d, errno = %d)\n", r, errno);
    return -1;
  }

  // int i = 0;
  // for (rp = result; rp != NULL; rp = rp->ai_next) {
  //     printf("[%d] ", i++); dump_ai(rp);
  // }

  int s;
  for (rp = result; rp != NULL; rp = rp->ai_next) {
    s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (s < 0) {
      printf("socket failed (errno = %d)\n", errno);
      continue;
    }
    if (connect(s, rp->ai_addr, rp->ai_addrlen) != -1) {
      printf("connected\n");
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

  int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) {
    printf("socket failed (errno = %d)\n", errno);
    return -1;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);

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
    printf("connected\n");
    dump_addr_in(&addr);

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
  printf("usage: socktest server port\n");
  printf("       socktest client address port message\n");
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
