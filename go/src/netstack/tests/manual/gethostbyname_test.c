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

void print_hostent(struct hostent* h) {
  printf("h_name: %s\n", h->h_name);
  for (int i = 0; h->h_aliases[i] != NULL; i++) {
    printf("h_aliases[%d]: %s\n", i, h->h_aliases[i]);
  }
  printf("h_addrtype: %d\n", h->h_addrtype);
  printf("h_length: %d\n", h->h_length);
  for (int i = 0; h->h_addr_list[i] != NULL; i++) {
    char buf[INET6_ADDRSTRLEN];
    switch (h->h_length) {
    case 4:
      printf("h_addr_list[%d]: %s\n",
             i, inet_ntop(AF_INET, h->h_addr_list[i], buf, sizeof(buf)));
      break;
    case 16:
      printf("h_addr_list[%d]: %s\n",
             i, inet_ntop(AF_INET6, h->h_addr_list[i], buf, sizeof(buf)));
      break;
    }
  }
}

void call_gethostbyname(char* name, int af, size_t buflen) {
  char* buf = malloc(buflen);

  struct hostent *res;
  int h_err;
  struct hostent h;

  int r = gethostbyname2_r(name, af, &h, buf, buflen, &res, &h_err);
  if (r != 0) {
    switch (r) {
    case ERANGE:
      printf("Buffer is too small (%zd bytes) to store the result\n", buflen);
      break;
    default:
      printf("Unknown return val: %d\n", r);
      break;
    }
  } else if (res == NULL) {
    switch (h_err) {
    case HOST_NOT_FOUND:
      printf("Host Not Found\n");
      break;
    case NO_RECOVERY:
      printf("No Recovery\n");
      break;
    case TRY_AGAIN:
      printf("Try Again\n");
      break;
    default:
      printf("h_err: %d\n", h_err);
      break;
    }
  } else {
    print_hostent(res);
  }

  free(buf);
}

void usage() {
  printf("usage: gethostbyname_test name [buflen (default:1024)]\n");
}

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    usage();
    return 1;
  }

  size_t buflen = 1024;
  if (argc == 3) {
    buflen = atoi(argv[2]);
  }

  printf("[AF_INET]\n");
  call_gethostbyname(argv[1], AF_INET, buflen);

  printf("\n[AF_INET6]\n");
  call_gethostbyname(argv[1], AF_INET6, buflen);

  return 0;
}
