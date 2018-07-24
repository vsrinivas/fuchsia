// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* sa_to_string(const struct sockaddr* sa, char* buf, size_t buflen) {
  if (sa == NULL) {
    return "<null>";
  }

  char addrstr[INET6_ADDRSTRLEN];
  if (sa->sa_family == AF_INET) {
    struct sockaddr_in* sin = (struct sockaddr_in*)sa;
    size_t n = snprintf(buf, buflen,
                        "inet4: %s (port %d)\n",
                        inet_ntop(AF_INET, &sin->sin_addr, addrstr, sizeof(addrstr)),
                        ntohs(sin->sin_port));
    if (n >= buflen) {
      return "<error: buffer overflow>";
    }
    return buf;
  } else if (sa->sa_family == AF_INET6) {
    struct sockaddr_in6* sin6 = (struct sockaddr_in6*)sa;
    size_t n = snprintf(buf, buflen,
                        "inet6: %s (port %d)\n",
                        inet_ntop(AF_INET6, &sin6->sin6_addr, addrstr, sizeof(addrstr)),
                        ntohs(sin6->sin6_port));
    if (n >= buflen) {
      return "<error: buffer overflow>";
    }
    return buf;
  }
  return "<error: unknown family>";
}

void print_ai(struct addrinfo* ai) {
  char sa_string_buf[256 + INET6_ADDRSTRLEN];
  printf("family %d, socktype %d, flags %d, protocol %d, addrlen %d, addr %s",
         ai->ai_family, ai->ai_socktype, ai->ai_flags, ai->ai_protocol,
         ai->ai_addrlen,
         sa_to_string(ai->ai_addr, sa_string_buf, sizeof(sa_string_buf)));
}

const char *name_to_string(const char* name) {
  if (name == NULL) {
    return "<null>";
  } else {
    return name;
  }
}

const char *family_to_string(int family) {
  if (family == AF_UNSPEC) {
  } else if (family == AF_INET) {
    return "inet4";
  } else if (family == AF_INET6) {
    return "inet6";
  }
  return "unknown";
}

const char *eai_to_string(int eai) {
  switch (eai) {
  case EAI_BADFLAGS:
    return "EAI_BADFLAGS";
  case EAI_NONAME:
    return "EAI_NONAME";
  case EAI_AGAIN:
    return "EAI_AGAIN";
  case EAI_FAIL:
    return "EAI_FAIL";
  case EAI_FAMILY:
    return "EAI_FAMILY";
  case EAI_SOCKTYPE:
    return "EAI_SOCKTYPE";
  case EAI_SERVICE:
    return "EAI_SERVICE";
  case EAI_MEMORY:
    return "EAI_MEMORY";
  case EAI_SYSTEM:
    return "EAI_SYSTEM";
  case EAI_OVERFLOW:
    return "EAI_OVERFLOW";
  }
  return "<unknown error>";
}

int getaddrinfo_test(const char* node, const char* service, int family) {
  printf("looking up node=%s service=%s family=%s...\n",
         name_to_string(node), name_to_string(service), family_to_string(family));

  struct addrinfo hints;
  struct addrinfo *result, *rp;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = family;
  hints.ai_socktype = SOCK_STREAM;

  int eai;
  eai = getaddrinfo(node, service, &hints, &result);
  if (eai != 0) {
    if (eai == EAI_SYSTEM) {
      printf("getaddrinfo failed (%s, errno = %d)\n", eai_to_string(eai), errno);
    } else {
      printf("getaddrinfo failed (%s)\n", eai_to_string(eai));
    }
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
  printf("usage: getaddrinfo_test [-n node][-s service][-f inet4 or inet6]\n");
}

int main(int argc, char** argv) {
  char *node = NULL;
  char *service = NULL;
  int family = AF_UNSPEC;

  int opt;
  while ((opt = getopt(argc, argv, "n:s:f:")) != -1) {
    switch (opt) {
    case 'n':
      node = optarg;
      break;
    case 's':
      service = optarg;
      break;
    case 'f':
      if (strcmp(optarg, "inet4") == 0) {
        family = AF_INET;
      } else if (strcmp(optarg, "inet6") == 0) {
        family = AF_INET6;
      } else {
        usage();
        return 1;
      }
      break;
    default:
      usage();
      return 1;
    }
  }

  if (getaddrinfo_test(node, service, family) < 0) {
    return 1;
  }

  return 0;
}
