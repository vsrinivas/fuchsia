// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "abr-client.h"

#include <endian.h>
#include <lib/cksum.h>

#include <zxtest/zxtest.h>

namespace {

constexpr abr::Data kAbrData = {
    .magic = {'\0', 'A', 'B', '0'},
    .version_major = abr::kMajorVersion,
    .version_minor = abr::kMinorVersion,
    .reserved1 = {},
    .slots =
        {
            {
                .priority = 0,
                .tries_remaining = 0,
                .successful_boot = 0,
                .reserved = {},
            },
            {
                .priority = 1,
                .tries_remaining = 0,
                .successful_boot = 1,
                .reserved = {},
            },
        },
    .oneshot_recovery_boot = 0,
    .reserved2 = {},
    .crc32 = {},
};

class TestClient : public abr::Client {
 public:
  const abr::Data& Data() const override { return data_; }

  zx_status_t Persist(abr::Data data) override { return ZX_OK; }

  void UpdateCrc() {
    data_.crc32 =
        htobe32(crc32(0, reinterpret_cast<const uint8_t*>(&data_), offsetof(abr::Data, crc32)));
  }

  abr::Data data_ = kAbrData;
};

TEST(AbrTest, InvalidCrc) {
  TestClient client;
  ASSERT_FALSE(client.IsValid());
}

TEST(AbrTest, InvalidMajorVersion) {
  TestClient client;
  client.data_.version_major = static_cast<uint8_t>(abr::kMajorVersion + 1);
  client.UpdateCrc();
  ASSERT_FALSE(client.IsValid());
}

TEST(AbrTest, InvalidAbrMinorVersion) {
  TestClient client;
  client.data_.version_minor = static_cast<uint8_t>(abr::kMinorVersion + 1);
  client.UpdateCrc();
  ASSERT_FALSE(client.IsValid());
}

TEST(AbrTest, InvalidPrioritySlot0) {
  TestClient client;
  client.data_.slots[0].priority = abr::kMaxPriority + 1;
  client.UpdateCrc();
  ASSERT_FALSE(client.IsValid());
}

TEST(AbrTest, InvalidPrioritySlot1) {
  TestClient client;
  client.data_.slots[1].priority = abr::kMaxPriority + 1;
  client.UpdateCrc();
  ASSERT_FALSE(client.IsValid());
}

TEST(AbrTest, InvalidTriesRemainingSlot0) {
  TestClient client;
  client.data_.slots[0].tries_remaining = abr::kMaxTriesRemaining + 1;
  client.UpdateCrc();
  ASSERT_FALSE(client.IsValid());
}

TEST(AbrTest, InvalidTriesRemainingSlot1) {
  TestClient client;
  client.data_.slots[1].tries_remaining = abr::kMaxTriesRemaining + 1;
  client.UpdateCrc();
  ASSERT_FALSE(client.IsValid());
}

TEST(AbrTest, IsValid) {
  TestClient client;
  client.UpdateCrc();
  ASSERT_TRUE(client.IsValid());
}

}  // namespace
