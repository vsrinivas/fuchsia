
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/sdp/pdu.h"
#include "garnet/drivers/bluetooth/lib/sdp/sdp.h"
#include "garnet/drivers/bluetooth/lib/sdp/status.h"

#include "gtest/gtest.h"

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"

namespace btlib {
namespace sdp {
namespace {

using common::LowerBits;
using common::UpperBits;

using SDP_PDUTest = ::testing::Test;

TEST_F(SDP_PDUTest, ErrorResponse) {
  ErrorResponse response;
  EXPECT_FALSE(response.complete());

  auto kInvalidContState = common::CreateStaticByteBuffer(
      0x01,        // opcode: kErrorResponse
      0xDE, 0xAD,  // transaction ID: 0xDEAD
      0x00, 0x02,  // parameter length: 2 bytes
      0x00, 0x05,  // ErrorCode: Invalid Continuation State
      0xFF, 0x00   // dummy extra bytes to cause an error
  );

  Status status = response.Parse(kInvalidContState.view(sizeof(Header)));
  EXPECT_FALSE(status);
  EXPECT_EQ(common::HostError::kPacketMalformed, status.error());

  status = response.Parse(kInvalidContState.view(sizeof(Header), 2));
  EXPECT_TRUE(status);
  EXPECT_TRUE(response.complete());
  EXPECT_EQ(ErrorCode::kInvalidContinuationState, response.error_code());

  response.set_error_code(ErrorCode::kInvalidContinuationState);
  auto ptr =
      response.GetPDU(0xF00F /* ignored */, 0xDEAD, common::BufferView());

  EXPECT_TRUE(ContainersEqual(kInvalidContState.view(0, 7), *ptr));
}

}  // namespace
}  // namespace sdp
}  // namespace btlib
