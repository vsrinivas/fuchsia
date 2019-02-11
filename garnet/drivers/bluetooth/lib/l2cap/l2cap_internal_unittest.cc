// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "l2cap_internal.h"

#include <iostream>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "gtest/gtest.h"

namespace btlib {
namespace l2cap {
namespace internal {
namespace {

using common::CreateStaticByteBuffer;

TEST(L2CAP_InternalTest, IdentifiesSupervisoryFrame) {
  // See Core Spec, v5, Vol 3, Part A, Table 3.2.
  EXPECT_TRUE(CreateStaticByteBuffer(0b00000001, 0)
                  .As<EnhancedControlField>()
                  .designates_supervisory_frame());
}

TEST(L2CAP_InternalTest, IdentifiesNonSupervisoryFrame) {
  // See Core Spec, v5, Vol 3, Part A, Table 3.2.
  EXPECT_FALSE(CreateStaticByteBuffer(0b00000000, 1)
                   .As<EnhancedControlField>()
                   .designates_supervisory_frame());
}

TEST(L2CAP_InternalTest, IdentifiesStartOfSegmentedSdu) {
  // See Core Spec, v5, Vol 3, Part A, Tables 3.2 and 3.4.
  EXPECT_TRUE(CreateStaticByteBuffer(0, 0b01000000)
                  .As<EnhancedControlField>()
                  .designates_start_of_segmented_sdu());
}

TEST(L2CAP_InternalTest, IdentifiesNonStartOfSegmentedSdu) {
  // See Core Spec, v5, Vol 3, Part A, Tables 3.2 and 3.4.
  EXPECT_FALSE(CreateStaticByteBuffer(0, 0b00000000)
                   .As<EnhancedControlField>()
                   .designates_start_of_segmented_sdu());
  EXPECT_FALSE(CreateStaticByteBuffer(0, 0b10000000)
                   .As<EnhancedControlField>()
                   .designates_start_of_segmented_sdu());
  EXPECT_FALSE(CreateStaticByteBuffer(0, 0b11000000)
                   .As<EnhancedControlField>()
                   .designates_start_of_segmented_sdu());
  EXPECT_FALSE(CreateStaticByteBuffer(1, 0b01000000)
                   .As<EnhancedControlField>()
                   .designates_start_of_segmented_sdu());
}

TEST(L2CAP_InternalTest, IdentifiesPartOfSegmentedSdu) {
  // See Core Spec, v5, Vol 3, Part A, Tables 3.2 and 3.4.
  EXPECT_TRUE(CreateStaticByteBuffer(0, 0b01000000)
                  .As<EnhancedControlField>()
                  .designates_part_of_segmented_sdu());
  EXPECT_TRUE(CreateStaticByteBuffer(0, 0b10000000)
                  .As<EnhancedControlField>()
                  .designates_part_of_segmented_sdu());
  EXPECT_TRUE(CreateStaticByteBuffer(0, 0b11000000)
                  .As<EnhancedControlField>()
                  .designates_part_of_segmented_sdu());
}

TEST(L2CAP_InternalTest, IdentifiesNotPartOfSegmentedSdu) {
  // See Core Spec, v5, Vol 3, Part A, Tables 3.2 and 3.4.
  EXPECT_FALSE(CreateStaticByteBuffer(0, 0b00000000)
                   .As<EnhancedControlField>()
                   .designates_part_of_segmented_sdu());
  EXPECT_FALSE(CreateStaticByteBuffer(1, 0b01000000)
                   .As<EnhancedControlField>()
                   .designates_part_of_segmented_sdu());
  EXPECT_FALSE(CreateStaticByteBuffer(1, 0b10000000)
                   .As<EnhancedControlField>()
                   .designates_part_of_segmented_sdu());
  EXPECT_FALSE(CreateStaticByteBuffer(1, 0b11000000)
                   .As<EnhancedControlField>()
                   .designates_part_of_segmented_sdu());
}

TEST(L2CAP_InternalTest, ReadsTxSequenceNumber) {
  // See Core Spec, v5, Vol 3, Part A, Table 3.2, and Core Spec v5, Vol 3, Part
  // A, Sec 8.3.
  for (uint8_t seq_num = 0; seq_num < 64; ++seq_num) {
    EXPECT_EQ(seq_num, CreateStaticByteBuffer(seq_num << 1, 0)
                           .As<SimpleInformationFrameHeader>()
                           .tx_seq());
  }
}

}  // namespace
}  // namespace internal
}  // namespace l2cap
}  // namespace btlib
