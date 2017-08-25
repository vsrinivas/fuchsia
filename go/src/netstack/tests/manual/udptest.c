// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
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

int client(const char* address, const char* service, const char* message,
           bool use_connect) {
  struct addrinfo hints;
  struct addrinfo *result, *rp;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = 0;
  hints.ai_protocol = 0;

  int r;
  r = getaddrinfo(address, service, &hints, &result);
  if (r != 0) {
    printf("getaddrinfo failed (%d, errno = %d)\n", r, errno);
    return -1;
  }

  // int i = 0;
  // for (rp = result; rp != NULL; rp = rp->ai_next) {
  //   printf("[%d] ", i++);
  //   dump_ai(rp);
  // }

  int s;
  for (rp = result; rp != NULL; rp = rp->ai_next) {
    s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (s < 0) {
      printf("socket failed (errno = %d)\n", errno);
      continue;
    }

    char str[INET6_ADDRSTRLEN];
    if (use_connect) {
      printf("connecting to %s\n", sa_to_str(rp->ai_addr, str, sizeof(str)));
      if (connect(s, rp->ai_addr, rp->ai_addrlen) < 0) {
        printf("connect failed (errno = %d)\n", errno);
        close(s);
        continue;
      }
      int nwrite;
      nwrite = write(s, message, strlen(message));
      if (nwrite < 0) {
        printf("write failed (%d) (errno = %d)\n", nwrite, errno);
        close(s);
        return -1;
      }
      printf("write success (nwrite = %d)\n", nwrite);
    } else {
      printf("sending to %s\n", sa_to_str(rp->ai_addr, str, sizeof(str)));
      int nsendto;
      nsendto =
          sendto(s, message, strlen(message), 0, rp->ai_addr, rp->ai_addrlen);
      if (nsendto < 0) {
        printf("sendto failed (%d) (errno = %d)\n", nsendto, errno);
        close(s);
        return -1;
      }
      printf("sendto success (nwrite = %d)\n", nsendto);
    }
    close(s);
    break;
  }
  if (rp == NULL) {
    printf("all connect attempts failed\n");
    return -1;
  }
  freeaddrinfo(result);

  return 0;
}
bool IsConnected(int socket_fd) {
  // Checks if connection is alive.
  char c;
  int rv = recv(socket_fd, &c, 1, MSG_PEEK);
  fprintf(stderr, "IsConnected: %d %d\n", rv, errno);
  if (rv == 0)
    return false;
  if (rv == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
    return false;

  return true;
}

int server(const char* service) {
  int16_t port = atoi(service);

  int s = socket(AF_INET6, SOCK_DGRAM, 0);
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

#define NTIMES 4

  for (int i = 0; i < NTIMES; i++) {
    printf("waiting for a connection on port %d...\n", port);
    IsConnected(s);

    for (;;) {
      char buf[128];
      int nrecv;
      socklen_t addrlen = sizeof(addr);
      nrecv =
          recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr*)&addr, &addrlen);
      if (nrecv < 0) {
        printf("recvfrom failed (%d) (errno = %d)\n", nrecv, errno);
        close(s);
        return -1;
      }
      char str[INET6_ADDRSTRLEN];
      printf("connected from %s\n",
             sa_to_str((struct sockaddr*)&addr, str, sizeof(str)));

      int n = write(1, buf, nrecv);
      if (n < 0) {
        printf("write failed (%d) (errno = %d)\n", n, errno);
        close(s);
        return -1;
      }
      printf("\n");
    }

    IsConnected(s);
  }
  close(s);

  return 0;
}

void usage(void) {
  printf("usage: udptest server port\n");
  printf("       udptest client address port message\n");
}

int main(int argc, char** argv) {
  if (argc > 1) {
    switch (argv[1][0]) {
      case 'c':  // client sendto
        if (argc == 5) return client(argv[2], argv[3], argv[4], false);
      case 'C':  // client connect & write
        if (argc == 5) return client(argv[2], argv[3], argv[4], true);
      case 's':  // server
        if (argc == 3) return server(argv[2]);
    }
  }
  usage();
  return -1;
}
