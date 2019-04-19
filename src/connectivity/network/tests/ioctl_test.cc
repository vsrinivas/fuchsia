// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/netstack/c/netconfig.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "gtest/gtest.h"

namespace {

TEST(NetstackTest, IoctlNetcGetNodename) {
  // gethostname calls uname, which bottoms out in a call to
  // ioctl_netc_get_nodename that isn't otherwise exposed in the SDK.
  char hostname[65];
  EXPECT_EQ(gethostname(hostname, sizeof(hostname)), 0) << strerror(errno);
  struct utsname uts;
  EXPECT_EQ(uname(&uts), 0) << strerror(errno);
}

TEST(NetstackTest, IoctlNetcGetNumIfs) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(fd, 0) << strerror(errno);

  netc_get_if_info_t get_if_info;
  ASSERT_GE(ioctl_netc_get_num_ifs(fd, &get_if_info.n_info), 0)
      << strerror(errno);

  for (uint32_t i = 0; i < get_if_info.n_info; i++) {
    ASSERT_GE(ioctl_netc_get_if_info_at(fd, &i, &get_if_info.info[i]), 0)
        << strerror(errno);
  }
}

}  // namespace
