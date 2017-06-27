// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// I got these results running this test on my local linux machine:
//
// SO_BROADCAST: default = off... setsockopt success = on
// SO_DEBUG: default = off... setsockopt error (13)
// SO_DONTROUTE: default = off... setsockopt success = on
// SO_ERROR: default = 0... setsockopt error (92)
// SO_KEEPALIVE: default = off... setsockopt success = on
// SO_LINGER: default = l_onoff:0, l_linger:0... setsockopt success = l_onoff:0, l_linger:0
// SO_OOBINLINE: default = off... setsockopt success = on
// SO_RCVBUF: default = 87380... setsockopt unchanged
// SO_SNDBUF: default = 16384... setsockopt unchanged
// SO_RCVLOWAT: default = 1... setsockopt success = 43
// SO_SNDLOWAT: default = 1... setsockopt error (92)
// SO_RCVTIMEO: default = 0s 0usec... setsockopt success = 0s 0usec
// SO_SNDTIMEO: default = 0s 0usec... setsockopt success = 0s 0usec
// SO_REUSEADDR: default = off... setsockopt success = on
// SO_REUSEPORT: default = off... setsockopt success = on
// SO_TYPE: default = 1... setsockopt error (92)
// IP_TOS: default = 0... setsockopt unchanged
// IP_TTL: default = 64... setsockopt success = 106
// IPV6_UNICAST_HOPS: default = 64... setsockopt success = 106
// IPV6_V6ONLY: default = off... setsockopt success = on
// TCP_NODELAY: default = off... setsockopt success = on
// TCP_MAXSEG: default = 536... setsockopt unchanged
// TCP_CORK: default = off... setsockopt success = on
// TCP_KEEPIDLE: default = 7200... setsockopt success = 7242
// TCP_KEEPINTVL: default = 75... setsockopt success = 117
// TCP_KEEPCNT: default = 9... setsockopt success = 51
// TCP_SYNCNT: default = 3... setsockopt success = 45
// TCP_LINGER2: default = 60... setsockopt unchanged
// TCP_DEFER_ACCEPT: default = 0... setsockopt unchanged
// TCP_WINDOW_CLAMP: default = 0... setsockopt unchanged
// TCP_INFO: default = size (16) not sizeof(int)... setsockopt error (92)
// TCP_QUICKACK: default = on... setsockopt success = off

#ifndef countof
#define countof sizeof
#endif

union val {
  int i_val;
  struct linger linger_val;
  struct timeval timeval_val;
};

static char strres[128];

static char* sock_str_flag(union val* ptr, int len) {
  if (len != sizeof(int))
    snprintf(strres, sizeof(strres), "size (%d) not sizeof(int)", len);
  else
    snprintf(strres, sizeof(strres), "%s", (ptr->i_val == 0) ? "off" : "on");
  return (strres);
}

static char* sock_str_int(union val* ptr, int len) {
  if (len != sizeof(int))
    snprintf(strres, sizeof(strres), "size (%d) not sizeof(int)", len);
  else
    snprintf(strres, sizeof(strres), "%d", ptr->i_val);
  return (strres);
}

static char* sock_str_linger(union val* ptr, int len) {
  struct linger* lptr = &ptr->linger_val;

  if (len != sizeof(struct linger))
    snprintf(strres, sizeof(strres), "size (%d) not sizeof(struct linger)",
             len);
  else
    snprintf(strres, sizeof(strres), "l_onoff:%d, l_linger:%d",
             lptr->l_onoff, lptr->l_linger);
  return (strres);
}

static char* sock_str_timeval(union val* ptr, int len) {
  struct timeval* tvptr = &ptr->timeval_val;

  if (len != sizeof(struct timeval))
    snprintf(strres, sizeof(strres), "size (%d) not sizeof(struct timeval)",
             len);
  else
    snprintf(strres, sizeof(strres), "%lds %ldusec", tvptr->tv_sec,
             tvptr->tv_usec);
  return (strres);
}

struct sock_opts {
  const char* opt_str;
  int opt_level;
  int opt_name;
  char* (*opt_val_str)(union val*, int);
} sock_opts_table[] = {
    {"SO_BROADCAST", SOL_SOCKET, SO_BROADCAST, sock_str_flag},
    {"SO_DEBUG", SOL_SOCKET, SO_DEBUG, sock_str_flag},
    {"SO_DONTROUTE", SOL_SOCKET, SO_DONTROUTE, sock_str_flag},
    {"SO_ERROR", SOL_SOCKET, SO_ERROR, sock_str_int},
    {"SO_KEEPALIVE", SOL_SOCKET, SO_KEEPALIVE, sock_str_flag},
    {"SO_LINGER", SOL_SOCKET, SO_LINGER, sock_str_linger},
    {"SO_OOBINLINE", SOL_SOCKET, SO_OOBINLINE, sock_str_flag},
    {"SO_RCVBUF", SOL_SOCKET, SO_RCVBUF, sock_str_int},
    {"SO_SNDBUF", SOL_SOCKET, SO_SNDBUF, sock_str_int},
    {"SO_RCVLOWAT", SOL_SOCKET, SO_RCVLOWAT, sock_str_int},
    {"SO_SNDLOWAT", SOL_SOCKET, SO_SNDLOWAT, sock_str_int},
    {"SO_RCVTIMEO", SOL_SOCKET, SO_RCVTIMEO, sock_str_timeval},
    {"SO_SNDTIMEO", SOL_SOCKET, SO_SNDTIMEO, sock_str_timeval},
    {"SO_REUSEADDR", SOL_SOCKET, SO_REUSEADDR, sock_str_flag},
    {"SO_REUSEPORT", SOL_SOCKET, SO_REUSEPORT, sock_str_flag},
    {"SO_TYPE", SOL_SOCKET, SO_TYPE, sock_str_int},
    {"IP_TOS", IPPROTO_IP, IP_TOS, sock_str_int},
    {"IP_TTL", IPPROTO_IP, IP_TTL, sock_str_int},
    {"IP_MULTICAST_TTL", IPPROTO_IP, IP_MULTICAST_TTL, sock_str_int},
    {"IPV6_UNICAST_HOPS", IPPROTO_IPV6, IPV6_UNICAST_HOPS, sock_str_int},
    {"IPV6_V6ONLY", IPPROTO_IPV6, IPV6_V6ONLY, sock_str_flag},
    {"TCP_NODELAY", IPPROTO_TCP, TCP_NODELAY, sock_str_flag},
    {"TCP_MAXSEG", IPPROTO_TCP, TCP_MAXSEG, sock_str_int},
    {"TCP_CORK", IPPROTO_TCP, TCP_CORK, sock_str_flag},
    {"TCP_KEEPIDLE", IPPROTO_TCP, TCP_KEEPIDLE, sock_str_int},
    {"TCP_KEEPINTVL", IPPROTO_TCP, TCP_KEEPINTVL, sock_str_int},
    {"TCP_KEEPCNT", IPPROTO_TCP, TCP_KEEPCNT, sock_str_int},
    {"TCP_SYNCNT", IPPROTO_TCP, TCP_SYNCNT, sock_str_int},
    {"TCP_LINGER2", IPPROTO_TCP, TCP_LINGER2, sock_str_int},
    {"TCP_DEFER_ACCEPT", IPPROTO_TCP, TCP_DEFER_ACCEPT, sock_str_int},
    {"TCP_WINDOW_CLAMP", IPPROTO_TCP, TCP_WINDOW_CLAMP, sock_str_int},
    {"TCP_INFO", IPPROTO_TCP, TCP_INFO, sock_str_int},
    {"TCP_QUICKACK", IPPROTO_TCP, TCP_QUICKACK, sock_str_flag},
    {NULL, 0, 0, NULL}
};

int test_setsockopt(int fd, struct sock_opts* ptr, union val *valp, socklen_t len) {
  if (setsockopt(fd, ptr->opt_level, ptr->opt_name, valp, len) == -1) {
    printf("setsockopt error (%d)", errno);
  } else {
    union val new_val;
    new_val.i_val = 0xdeadbeef;
    socklen_t new_len = sizeof(new_val);
    if (getsockopt(fd, ptr->opt_level, ptr->opt_name, &new_val, &new_len) == -1) {
      printf("getsockopt error (%d)", errno);
    } else if (new_val.i_val == (int)0xdeadbeef) {
      printf("setsockopt unchanged");
    } else if (len != new_len) {
      printf("getsockopt returned a different size (%d) than expected (%d)",
             new_len, len);
    } else if (memcmp(valp, &new_val, len) != 0) {
      printf("getsockopt returned a different val (%s)",
             ptr->opt_val_str(&new_val, new_len));
      printf(" than expected (%s)", ptr->opt_val_str(valp, len));
    } else {
      printf("setsockopt success = %s", ptr->opt_val_str(valp, len));
      return 0;
    }
  }
  return -1;
}

int main(int argc, char** argv) {
  socklen_t len;
  struct sock_opts* ptr;

  for (ptr = sock_opts_table; ptr->opt_str != NULL; ptr++) {
    int fd = -1;
    int sock_type = SOCK_STREAM;
    if (ptr->opt_name == IP_MULTICAST_TTL)
      sock_type = SOCK_DGRAM;
    switch (ptr->opt_level) {
      case SOL_SOCKET:
      case IPPROTO_IP:
      case IPPROTO_TCP:
        fd = socket(AF_INET, sock_type, 0);
        break;
      case IPPROTO_IPV6:
        fd = socket(AF_INET6, sock_type, 0);
        break;
    }

    printf("%s: ", ptr->opt_str);
    if (ptr->opt_val_str == NULL)
      printf("(undefined)\n");
    else {
      union val ini_val;
      len = sizeof(ini_val);
      if (getsockopt(fd, ptr->opt_level, ptr->opt_name, &ini_val, &len) == -1) {
        printf("getsockopt error (%d)... ", errno);
      } else {
        printf("initial = %s... ", ptr->opt_val_str(&ini_val, len));
      }

      // Change the option and see if it was successful.
      union val val = ini_val;
      if (ptr->opt_val_str == sock_str_flag) {
        val.i_val = !val.i_val;
      } else if (ptr->opt_val_str == sock_str_int) {
        val.i_val += 42;
      }
      if (test_setsockopt(fd, ptr, &val, len) == 0) {
        printf("... ");
        // Change the option back to the initial value.
        test_setsockopt(fd, ptr, &ini_val, len);
      }
      printf ("\n");
    }
  }

  return 0;
}
