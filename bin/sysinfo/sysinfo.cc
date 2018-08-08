// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <getopt.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

#include "lib/netstack/c/netconfig.h"

void PrintUsage() {
  // clang-format off
  fprintf(stderr, "Usage: sysinfo <options>...\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "  -l (--hostname)           Print the system's hostname \n");
  fprintf(stderr, "  -4 (--ipv4)               Print the system's ipv4 addresses \n");
  fprintf(stderr, "  -6 (--ipv6)               Print the system's ipv6 addresses \n");
  fprintf(stderr, "  -v (--verbose)            Verbose output \n");
  fprintf(stderr, "  -h (--help)               Print usage \n");
  putchar('\n');
  // clang-format on
}

bool PrintHostname(bool verbose) {
  char host_name_buffer[HOST_NAME_MAX + 1];
  int result = gethostname(host_name_buffer, sizeof(host_name_buffer));

  if (result < 0) {
    perror("gethostname failed");
    return false;
  }

  host_name_buffer[sizeof(host_name_buffer) - 1] = '\0';

  if (verbose) {
    fprintf(stdout, "Host: \t%s\n", host_name_buffer);
  } else {
    fprintf(stdout, "%s\n", host_name_buffer);
  }
  return true;
}

bool PrintIPAddresses(bool print_ipv4, bool print_ipv6, bool verbose) {
  // create a socket so we can check all the addresses it listens on.
  int fd = socket(AF_INET6, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("unable to create socket");
    return false;
  }

  netc_get_if_info_t get_if_info;
  const ssize_t size = ioctl_netc_get_num_ifs(fd, &get_if_info.n_info);
  if (size < 0 || get_if_info.n_info == 0) {
    perror("no interfaces from ioctl_netc_get_if_info");
    close(fd);
    return false;
  }

  for (uint32_t i = 0; i < get_if_info.n_info; ++i) {
    const ssize_t size =
        ioctl_netc_get_if_info_at(fd, &i, &get_if_info.info[i]);
    if (size < 0) {
      perror("ioctl_netc_get_if_info_at failed");
      close(fd);
      return false;
    }
    netc_if_info_t* if_info = &get_if_info.info[i];

    if (if_info->addr.ss_family == AF_INET && print_ipv4) {
      sockaddr_in ip4 = *reinterpret_cast<const sockaddr_in*>(&if_info->addr);
      char str[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &ip4.sin_addr, str, INET_ADDRSTRLEN);
      if (verbose) {
        fprintf(stdout, "IPv4: \t%s\n", str);
      } else {
        fprintf(stdout, "%s\n", str);
      }
    }

    if (if_info->addr.ss_family == AF_INET6 && print_ipv6) {
      sockaddr_in6 ip6 = *reinterpret_cast<const sockaddr_in6*>(&if_info->addr);
      char str[INET6_ADDRSTRLEN];
      inet_ntop(AF_INET6, &ip6.sin6_addr, str, INET6_ADDRSTRLEN);
      if (verbose) {
        fprintf(stdout, "IPv6: \t%s\n", str);
      } else {
        fprintf(stdout, "%s\n", str);
      }
    }
  }
  close(fd);
  return true;
}

int main(int argc, char* const argv[]) {
  // clang-format off
  static struct option long_options[] = {
    { "hostname",  no_argument, 0, 'l' },
    { "ipv4",      no_argument, 0, '4' },
    { "ipv6",      no_argument, 0, '6' },
    { "help",      no_argument, 0, 'h' },
    { "verbose",   no_argument, 0, 'v' },
  };
  // clang-format on

  bool opts_done = false;
  bool specific = false;
  bool print_hostname = false;
  bool print_ipv4 = false;
  bool print_ipv6 = false;
  bool verbose = false;

  while (!opts_done) {
    switch (getopt_long(argc, argv, "l46hv", long_options, NULL)) {
      case 'l':
        specific = true;
        print_hostname = true;
        break;
      case '4':
        specific = true;
        print_ipv4 = true;
        break;
      case '6':
        specific = true;
        print_ipv6 = true;
        break;
      case 'v':
        verbose = true;
        break;
      case 'h':
        PrintUsage();
        return 0;
      case -1:
        opts_done = true;
    }
  }

  if (!specific) {
    // no specific thing to print, so print all these by default
    print_hostname = true;
    print_ipv4 = true;
    print_ipv6 = true;
    verbose = true;
  }

  if (print_hostname) {
    if (!PrintHostname(verbose))
      return 1;
  }

  if (print_ipv4 || print_ipv6) {
    if (!PrintIPAddresses(print_ipv4, print_ipv6, verbose))
      return 1;
  }

  return 0;
}
