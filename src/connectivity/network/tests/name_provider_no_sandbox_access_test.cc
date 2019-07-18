// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This test should run with no access to fuchsia.device.NameProvider.

#include <fuchsia/device/cpp/fidl.h>
#include <sys/utsname.h>
#include <limits.h>

#include "gtest/gtest.h"

namespace {

TEST(NoNetworkTest, GetHostNameDefault) {
  char hostname[HOST_NAME_MAX];
  ASSERT_EQ(gethostname(hostname, sizeof(hostname)), -1);
  ASSERT_EQ(errno, EPIPE) << strerror(errno);
}

TEST(NoNetworkTest, UnameDefault) {
  utsname uts;
  ASSERT_EQ(uname(&uts), -1);
  ASSERT_EQ(errno, EPIPE) << strerror(errno);
}

}  // namespace
