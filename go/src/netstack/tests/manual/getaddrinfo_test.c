// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

void print_ai(struct addrinfo* ai) {
  char str[INET6_ADDRSTRLEN];
  printf("family %d, socktype %d, flags %d, protocol %d, addrlen %d, addr %s\n",
         ai->ai_family, ai->ai_socktype, ai->ai_flags, ai->ai_protocol,
         ai->ai_addrlen,
         ai->ai_addr ? sa_to_str(ai->ai_addr, str, sizeof(str)) : "NULL");
}

int getaddrinfo_test(const char* address, int family) {
  switch (family) {
  case AF_UNSPEC:
    printf("looking up %s ...\n", address);
    break;
  case AF_INET:
    printf("looking up %s for v4 (family %d)...\n", address, family);
    break;
  case AF_INET6:
    printf("looking up %s for v6 (family %d)...\n", address, family);
    break;
  }

  struct addrinfo hints;
  struct addrinfo *result, *rp;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = family;
  hints.ai_socktype = SOCK_STREAM;

  int r;
  r = getaddrinfo(address, NULL, &hints, &result);
  if (r != 0) {
    printf("getaddrinfo failed (%d, errno = %d)\n", r, errno);
    return -1;
  }

  int i = 0;
  for (rp = result; rp != NULL; rp = rp->ai_next) {
    printf("[%d] ", i++);
    print_ai(rp);
  }

  freeaddrinfo(result);
  return 0;
}

void usage() {
  printf("usage: getaddrinfo_test address [4 or 6]\n");
}

int main(int argc, char** argv) {
  if (argc != 2 && argc != 3) {
    usage();
    return 1;
  }

  char* name = argv[1];

  int family = AF_UNSPEC;
  if (argc == 3) {
    int n = atoi(argv[2]);
    if (n == 4) {
      family = AF_INET;
    } else if (n == 6) {
      family = AF_INET6;
    } else {
      usage();
      return 1;
    }
  }

  if (getaddrinfo_test(name, family) < 0) {
    return 1;
  }

  return 0;
}
