// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/netstack/c/netconfig.h>

#include "gtest/gtest.h"

namespace {

TEST(NetstackTest, IoctlNetcGetNumIfs) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(fd, 0) << strerror(errno);

  netc_get_if_info_t get_if_info;
  ASSERT_GE(ioctl_netc_get_num_ifs(fd, &get_if_info.n_info), 0) << strerror(errno);

  for (uint32_t i = 0; i < get_if_info.n_info; i++) {
    ASSERT_GE(ioctl_netc_get_if_info_at(fd, &i, &get_if_info.info[i]), 0) << strerror(errno);
  }
}

}  // namespace
