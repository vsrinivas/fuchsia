// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.tee/cpp/wire.h>
#include <fuchsia/hardware/tee/c/banjo.h>

#include <zxtest/zxtest.h>

#include "optee-util.h"

namespace optee {
namespace test {

TEST(UuidTest, LlcppCtor) {
  fuchsia_tee::wire::Uuid llcpp_uuid{
      .time_low = 0x01234567,
      .time_mid = 0x89AB,
      .time_hi_and_version = 0xCDEF,
      .clock_seq_and_node = {0x01, 0x02, 0x03, 0x04, 0x05, 0x6, 0x07, 0x08}};

  Uuid uuid(llcpp_uuid);

  EXPECT_EQ(uuid.time_low(), 0x01234567);
  EXPECT_EQ(uuid.time_mid(), 0x89AB);
  EXPECT_EQ(uuid.time_hi_and_version(), 0xCDEF);
  for (size_t i = 0; i < 8; i++) {
    EXPECT_EQ(uuid.clock_seq_and_node()[i], i + 1);
  }
}

TEST(UuidTest, BanjoCtor) {
  uuid_t banjo_uuid{.time_low = 0x01234567,
                    .time_mid = 0x89AB,
                    .time_hi_and_version = 0xCDEF,
                    .clock_seq_and_node = {0x01, 0x02, 0x03, 0x04, 0x05, 0x6, 0x07, 0x08}};

  Uuid uuid(banjo_uuid);

  EXPECT_EQ(uuid.time_low(), 0x01234567);
  EXPECT_EQ(uuid.time_mid(), 0x89AB);
  EXPECT_EQ(uuid.time_hi_and_version(), 0xCDEF);
  for (size_t i = 0; i < 8; i++) {
    EXPECT_EQ(uuid.clock_seq_and_node()[i], i + 1);
  }
}

TEST(UuidTest, OctetCtor) {
  Uuid::Octets octets_uuid = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
                              0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

  Uuid uuid(octets_uuid);

  EXPECT_EQ(uuid.time_low(), 0x01234567);
  EXPECT_EQ(uuid.time_mid(), 0x89AB);
  EXPECT_EQ(uuid.time_hi_and_version(), 0xCDEF);
  for (size_t i = 0; i < 8; i++) {
    EXPECT_EQ(uuid.clock_seq_and_node()[i], i + 1);
  }
}

TEST(UuidTest, ToOctets) {
  Uuid uuid{0x01234567, 0x89AB, 0xCDEF, {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}};

  Uuid::Octets expected_octets = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
                                  0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  EXPECT_EQ(uuid.ToOctets(), expected_octets);
}

TEST(UuidTest, ToString) {
  Uuid uuid{0x01234567, 0x89AB, 0xCDEF, {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}};

  EXPECT_STREQ(uuid.ToString(), "01234567-89ab-cdef-0102-030405060708");
}

}  // namespace test
}  // namespace optee
