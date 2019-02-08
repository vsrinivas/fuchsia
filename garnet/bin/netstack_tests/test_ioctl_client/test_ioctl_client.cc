// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/netstack/c/netconfig.h>

#include <cerrno>
#include <string>

#include <arpa/inet.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

int main() {
  const int fd = socket(AF_INET6, SOCK_STREAM, 0);
  if (fd < 0) {
    fprintf(stderr, "socket(AF_INET, SOCK_DGRAM, 0) failed: errno: %d\n",
            errno);
    return 1;
  }

  netc_get_if_info_t get_if_info;
  const ssize_t size = ioctl_netc_get_num_ifs(fd, &get_if_info.n_info);
  if (size < 0) {
    fprintf(stderr, "ioctl_netc_get_num_ifs() failed: errno: %d\n", errno);
    return 1;
  }

  for (uint32_t i = 0; i < get_if_info.n_info; i++) {
    const ssize_t size =
        ioctl_netc_get_if_info_at(fd, &i, &get_if_info.info[i]);
    if (size < 0) {
      fprintf(stderr, "ioctl_netc_get_if_info_at() failed\n: errno: %d", errno);
      return 1;
    }
  }
  return 0;
}
