// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/time.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>
#include <openthread/platform/settings.h>

static constexpr size_t kTestDataSize = 60;

// Most tests with multiple records store records of different
// size to help verify the read size
constexpr size_t kFirstRecordSize = kTestDataSize;
constexpr size_t kSecondRecordSize = kTestDataSize / 2;
constexpr size_t kThirdRecordSize = kTestDataSize / 3;

class SettingsTest : public ::gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();
    otPlatSettingsInit(instance);
    otPlatSettingsWipe(instance);

    // Initialize data with some non-zero value
    // picked the ASCII value of a printable character
    // as it can be printed easily if required for debug
    memset(data, 65, kTestDataSize);
  }

  void TearDown() override {
    otPlatSettingsWipe(instance);
    otPlatSettingsDeinit(instance);
    TestLoopFixture::TearDown();
  }

 protected:
  uint8_t data[kTestDataSize];
  otInstance *instance = NULL;
};

TEST_F(SettingsTest, EmptyConfig) {
  // verify empty situation
  uint8_t value[sizeof(data)];
  uint16_t length = sizeof(value);

  EXPECT_EQ(otPlatSettingsGet(instance, 0, 0, value, &length), OT_ERROR_NOT_FOUND);
  EXPECT_EQ(otPlatSettingsDelete(instance, 0, 0), OT_ERROR_NOT_FOUND);
  EXPECT_EQ(otPlatSettingsDelete(instance, 0, -1), OT_ERROR_NOT_FOUND);
}

TEST_F(SettingsTest, SingleConfig) {
  constexpr size_t kStoreDataSize = sizeof(data) / 2;
  EXPECT_EQ(otPlatSettingsSet(instance, 0, data, kStoreDataSize), OT_ERROR_NONE);

  uint8_t value[sizeof(data)];
  uint16_t length = sizeof(value);

  // Check with both parameters NULL
  EXPECT_EQ(otPlatSettingsGet(instance, 0, 0, NULL, NULL), OT_ERROR_NONE);

  // Check with only one parameter NULL
  EXPECT_EQ(otPlatSettingsGet(instance, 0, 0, NULL, &length), OT_ERROR_NONE);
  EXPECT_EQ(length, kStoreDataSize);

  // Check with no parameter NULL
  length = sizeof(value);
  EXPECT_EQ(otPlatSettingsGet(instance, 0, 0, value, &length), OT_ERROR_NONE);
  EXPECT_EQ(length, kStoreDataSize);
  EXPECT_EQ(0, memcmp(value, data, length));

  // Check with insufficient buffer
  length = kStoreDataSize;
  length -= 1;
  value[length] = 0;
  EXPECT_EQ(otPlatSettingsGet(instance, 0, 0, value, &length), OT_ERROR_NONE);
  // verify length becomes the actual length of the record
  EXPECT_EQ(length, kStoreDataSize);
  // verify this byte is not changed
  EXPECT_EQ(value[length - 1], 0);

  // wrong index
  EXPECT_EQ(otPlatSettingsGet(instance, 0, 1, NULL, NULL), OT_ERROR_NOT_FOUND);
  // wrong key
  EXPECT_EQ(otPlatSettingsGet(instance, 1, 0, NULL, NULL), OT_ERROR_NOT_FOUND);
}

TEST_F(SettingsTest, TwoRecordsSameKey) {
  EXPECT_EQ(otPlatSettingsSet(instance, 0, data, kFirstRecordSize), OT_ERROR_NONE);
  EXPECT_EQ(otPlatSettingsAdd(instance, 0, data, kSecondRecordSize), OT_ERROR_NONE);
  uint8_t value[sizeof(data)];
  uint16_t length = sizeof(value);

  EXPECT_EQ(otPlatSettingsGet(instance, 0, 1, value, &length), OT_ERROR_NONE);
  EXPECT_EQ(length, kSecondRecordSize);
  EXPECT_EQ(0, memcmp(value, data, length));

  length = sizeof(value);
  EXPECT_EQ(otPlatSettingsGet(instance, 0, 0, value, &length), OT_ERROR_NONE);
  EXPECT_EQ(length, kFirstRecordSize);
  EXPECT_EQ(0, memcmp(value, data, length));
}

TEST_F(SettingsTest, TwoRecordsDifferentKeys) {
  EXPECT_EQ(otPlatSettingsSet(instance, 0, data, kFirstRecordSize), OT_ERROR_NONE);
  EXPECT_EQ(otPlatSettingsAdd(instance, 1, data, kSecondRecordSize), OT_ERROR_NONE);

  uint8_t value[sizeof(data)];
  uint16_t length = sizeof(value);

  EXPECT_EQ(otPlatSettingsGet(instance, 1, 0, value, &length), OT_ERROR_NONE);
  EXPECT_EQ(length, kSecondRecordSize);
  EXPECT_EQ(0, memcmp(value, data, length));

  length = sizeof(value);
  EXPECT_EQ(otPlatSettingsGet(instance, 0, 0, value, &length), OT_ERROR_NONE);
  EXPECT_EQ(length, kFirstRecordSize);
  EXPECT_EQ(0, memcmp(value, data, length));
}

TEST_F(SettingsTest, VerifyDeleteRecords) {
  EXPECT_EQ(otPlatSettingsAdd(instance, 0, data, kFirstRecordSize), OT_ERROR_NONE);
  EXPECT_EQ(otPlatSettingsAdd(instance, 0, data, kSecondRecordSize), OT_ERROR_NONE);
  EXPECT_EQ(otPlatSettingsAdd(instance, 0, data, kThirdRecordSize), OT_ERROR_NONE);
  uint8_t value[sizeof(data)];
  uint16_t length = sizeof(value);

  // wrong key
  EXPECT_EQ(otPlatSettingsDelete(instance, 1, 0), OT_ERROR_NOT_FOUND);
  EXPECT_EQ(otPlatSettingsDelete(instance, 1, -1), OT_ERROR_NOT_FOUND);

  // wrong index
  EXPECT_EQ(otPlatSettingsDelete(instance, 0, 3), OT_ERROR_NOT_FOUND);

  // delete one record
  EXPECT_EQ(otPlatSettingsDelete(instance, 0, 1), OT_ERROR_NONE);
  EXPECT_EQ(otPlatSettingsGet(instance, 0, 1, value, &length), OT_ERROR_NONE);
  EXPECT_EQ(length, kThirdRecordSize);
  EXPECT_EQ(0, memcmp(value, data, length));

  // delete all records
  EXPECT_EQ(otPlatSettingsDelete(instance, 0, -1), OT_ERROR_NONE);
  EXPECT_EQ(otPlatSettingsGet(instance, 0, 0, NULL, NULL), OT_ERROR_NOT_FOUND);
}

// Make sure that deleting record of one type doesn't affect
// records of another type (ie records with different key)
TEST_F(SettingsTest, VerifyDeleteByType) {
  // verify delete all records of a type
  EXPECT_EQ(otPlatSettingsAdd(instance, 0, data, kFirstRecordSize), OT_ERROR_NONE);
  EXPECT_EQ(otPlatSettingsAdd(instance, 1, data, kSecondRecordSize), OT_ERROR_NONE);
  EXPECT_EQ(otPlatSettingsAdd(instance, 0, data, kThirdRecordSize), OT_ERROR_NONE);
  uint8_t value[sizeof(data)];
  uint16_t length = sizeof(value);

  EXPECT_EQ(otPlatSettingsDelete(instance, 0, -1), OT_ERROR_NONE);
  EXPECT_EQ(otPlatSettingsGet(instance, 0, 0, value, &length), OT_ERROR_NOT_FOUND);
  EXPECT_EQ(otPlatSettingsGet(instance, 1, 0, value, &length), OT_ERROR_NONE);
  EXPECT_EQ(length, kSecondRecordSize);
  EXPECT_EQ(0, memcmp(value, data, length));

  EXPECT_EQ(otPlatSettingsDelete(instance, 0, 0), OT_ERROR_NOT_FOUND);
  EXPECT_EQ(otPlatSettingsGet(instance, 0, 0, NULL, NULL), OT_ERROR_NOT_FOUND);
}
