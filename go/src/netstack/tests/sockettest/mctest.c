// Copyright 2017 The Fuchsia Authors. All rights reserved.
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

int client(const char* if_address,
           const char* mc_address,
           const char* service) {
  unsigned short port = atoi(service);

  int s = socket(PF_INET, SOCK_DGRAM, 0);
  if (s < 0) {
    printf("socket failed (errno = %d)\n", errno);
    return -1;
  }

  unsigned char mc_ttl = 1;
  if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &mc_ttl, sizeof(mc_ttl)) <
      0) {
    printf("setsockopt: IP_MULTICAST_TTL failed (errno = %d)\n", errno);
    close(s);
    return -1;
  }

  struct in_addr if_addr;
  if_addr.s_addr = inet_addr(if_address);
  if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, &if_addr, sizeof(if_addr)) <
      0) {
    printf("setsockopt: IP_MULTICAST_IF failed (errno = %d)\n", errno);
    close(s);
    return -1;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr(mc_address);
  addr.sin_port = htons(port);

  char str[INET6_ADDRSTRLEN];
  printf("sending to %s\n",
         sa_to_str((struct sockaddr*)&addr, str, sizeof(str)));

  char message[1024];
  memset(message, 0, sizeof(message));
  while (fgets(message, sizeof(message), stdin)) {
    if (message[0] == '\n')
      break;

    int nsendto;
    nsendto = sendto(s, message, strlen(message), 0, (struct sockaddr*)&addr,
                     sizeof(addr));
    if (nsendto < 0) {
      printf("sendto failed (%d) (errno = %d)\n", nsendto, errno);
      close(s);
      return -1;
    }
  }
  close(s);

  return 0;
}

int server(const char* if_address,
           const char* mc_address,
           const char* service) {
  unsigned short port = atoi(service);

  int s = socket(PF_INET, SOCK_DGRAM, 0);
  if (s < 0) {
    printf("socket failed (errno = %d)\n", errno);
    return -1;
  }

  int on = 1;
  if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
    printf("setsockopt: SO_REUSEADDR failed (errno = %d)\n", errno);
    close(s);
    return -1;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);

  if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    printf("bind failed (errno = %d)\n", errno);
    close(s);
    return -1;
  }

  struct ip_mreq mc_req;
  mc_req.imr_multiaddr.s_addr = inet_addr(mc_address);
  mc_req.imr_interface.s_addr = inet_addr(if_address);

  if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mc_req, sizeof(mc_req)) <
      0) {
    printf("setsockopt: IP_ADD_MEMBERSHIP failed (errno = %d)\n", errno);
    close(s);
    return -1;
  }

#define NTIMES 4

  for (int i = 0; i < NTIMES; i++) {
    printf("waiting %s:%d...\n", mc_address, port);

    char buf[128];
    int nrecv;
    socklen_t addrlen = sizeof(addr);
    nrecv = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr*)&addr, &addrlen);
    if (nrecv < 0) {
      printf("recvfrom failed (%d) (errno = %d)\n", nrecv, errno);
      close(s);
      return -1;
    }
    char str[INET6_ADDRSTRLEN];
    printf("received %d bytes from %s\n", nrecv,
           sa_to_str((struct sockaddr*)&addr, str, sizeof(str)));
  }

  if (setsockopt(s, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mc_req, sizeof(mc_req)) <
      0) {
    printf("setsockopt: IP_ADD_MEMBERSHIP failed (errno = %d)\n", errno);
    close(s);
    return -1;
  }

  close(s);

  return 0;
}

void usage(void) {
  printf("usage: mctest server if_address [multicast_address port]\n");
  printf("       mctest client if_address [multicast_address port]\n");
}

int main(int argc, char** argv) {
  if (argc == 3 || argc == 5) {
    char* mc_address = "232.43.211.234";
    char* service = "4321";
    if (argc == 5) {
      mc_address = argv[3];
      service = argv[4];
    }
    switch (argv[1][0]) {
      case 'c':  // client
        return client(argv[2], mc_address, service);
      case 's':  // server
        return server(argv[2], mc_address, service);
    }
  }
  usage();
  return -1;
}
