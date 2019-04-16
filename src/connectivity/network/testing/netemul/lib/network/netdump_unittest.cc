// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/testing/netemul/lib/network/netdump.h"

#include "gtest/gtest.h"
#include "src/connectivity/network/testing/netemul/lib/network/netdump_parser.h"

namespace netemul {
namespace testing {

const uint8_t kTestBytes[] = {0x00, 0x01, 0x02, 0x03, 0x04,
                              0x05, 0x06, 0x07, 0x08, 0x09};

class NetworkDumpTest : public ::testing::Test {};

TEST_F(NetworkDumpTest, ExpectedDump) {
  InMemoryDump dump;
  dump.AddInterface("test-net1");
  dump.AddInterface("test-net2");
  dump.WritePacket(kTestBytes, sizeof(kTestBytes), 0);
  dump.WritePacket(kTestBytes, sizeof(kTestBytes), 1);
  dump.WritePacket(kTestBytes, sizeof(kTestBytes) / 2, 1);

  NetDumpParser parser;
  auto bytes = dump.CopyBytes();
  ASSERT_TRUE(parser.Parse(&bytes[0], bytes.size()));
  ASSERT_EQ(parser.interfaces().size(), 2ul);
  ASSERT_EQ(parser.packets().size(), 3ul);

  EXPECT_EQ(parser.interfaces()[0], "test-net1");
  EXPECT_EQ(parser.interfaces()[1], "test-net2");

  EXPECT_EQ(parser.packets()[0].interface, 0u);
  EXPECT_EQ(parser.packets()[1].interface, 1u);
  EXPECT_EQ(parser.packets()[2].interface, 1u);

  EXPECT_EQ(parser.packets()[0].len, sizeof(kTestBytes));
  EXPECT_EQ(parser.packets()[1].len, sizeof(kTestBytes));
  EXPECT_EQ(parser.packets()[2].len, sizeof(kTestBytes) / 2);

  EXPECT_EQ(
      memcmp(parser.packets()[0].data, kTestBytes, parser.packets()[0].len), 0);
  EXPECT_EQ(
      memcmp(parser.packets()[1].data, kTestBytes, parser.packets()[1].len), 0);
  EXPECT_EQ(
      memcmp(parser.packets()[2].data, kTestBytes, parser.packets()[2].len), 0);
}

}  // namespace testing
}  // namespace netemul
