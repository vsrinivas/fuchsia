// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests ensure fdio can talk to netstack.
// No network connection is required, only a running netstack binary.

#include <net/if.h>

#include "gtest/gtest.h"

namespace {

TEST(IfNameIndexTest, IfNameIndexLookupRoundtrip) {
  char ifname[IF_NAMESIZE] = "";
  ASSERT_NE(if_indextoname(1, ifname), nullptr) << strerror(errno);
  EXPECT_EQ(if_nametoindex(ifname), static_cast<unsigned>(1)) << strerror(errno);
}

TEST(IfNameIndexTest, IfIndexToNameNotFound) {
  char ifname[IF_NAMESIZE] = "";
  ASSERT_EQ(if_indextoname(0, ifname), nullptr);
  EXPECT_EQ(errno, ENXIO) << strerror(errno);
}

TEST(IfNameIndexTest, IfNameToIndexNotFound) {
  char ifname[IF_NAMESIZE] = "";
  ASSERT_EQ(if_nametoindex(ifname), static_cast<unsigned>(0));
  EXPECT_EQ(errno, ENODEV) << strerror(errno);
}
}  // namespace
