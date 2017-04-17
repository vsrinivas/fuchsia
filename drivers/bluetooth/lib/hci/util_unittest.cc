// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/bluetooth/lib/hci/util.h"

#include "gtest/gtest.h"

#include "apps/bluetooth/lib/common/test_helpers.h"

namespace bluetooth {
namespace hci {
namespace {

constexpr hci::OpCode kTestOpCode = hci::VendorOpCode(0xFF);

struct TestParams {
  uint8_t param0;
  uint16_t param1;
} __PACKED;

TEST(HCIUtilTest, BuildHCICommand) {
  auto expected0 = common::CreateStaticByteBuffer(0xFF, 0xFC, 0x00);
  auto command = BuildHCICommand(kTestOpCode);
  EXPECT_TRUE(common::ContainersEqual(expected0, command));

  auto expected1 = common::CreateStaticByteBuffer(0xFF, 0xFC, 0x03, 0x01, 0xFF, 0x00);

  TestParams params;
  params.param0 = 1;
  params.param1 = 255;
  command = BuildHCICommand(kTestOpCode, &params, sizeof(params));
  EXPECT_TRUE(common::ContainersEqual(expected1, command));
}

}  // namespace
}  // namespace hci
}  // namespace adapter
