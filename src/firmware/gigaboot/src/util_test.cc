// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <gtest/gtest.h>

namespace {

TEST(Util, Htonll) {
  const uint64_t host_order = 0x0123456789ABCDEFULL;
  const uint64_t net_order = htonll(host_order);

  std::vector<uint8_t> data(sizeof(net_order));
  memcpy(data.data(), &net_order, sizeof(net_order));
  EXPECT_EQ(data, (std::vector<uint8_t>{0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF}));
}

TEST(Util, Ntohll) {
  std::vector<uint8_t> data{0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
  uint64_t net_order;
  memcpy(&net_order, data.data(), sizeof(net_order));

  const uint64_t host_order = ntohll(net_order);
  EXPECT_EQ(host_order, 0x0123456789ABCDEFULL);
}

}  // namespace
