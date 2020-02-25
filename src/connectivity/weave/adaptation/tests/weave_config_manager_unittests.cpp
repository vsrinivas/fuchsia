// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/gtest/test_loop_fixture.h>
#include <string.h>

#include <gtest/gtest.h>

#include "src/connectivity/weave/adaptation/weave_config_manager.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"

namespace nl::Weave::DeviceLayer::Internal {
namespace testing {
namespace {
constexpr char kWeaveConfigStoreTestPath[] = "/data/config.json";
}  // namespace

class WeaveConfigManagerTest : public ::gtest::TestLoopFixture {
 public:
  void SetUp() override { TestLoopFixture::SetUp(); }

  void TearDown() override {
    EXPECT_EQ(WeaveConfigMgr().FactoryResetConfig(), WEAVE_NO_ERROR);
    EXPECT_TRUE(files::DeletePath(kWeaveConfigStoreTestPath, false));
    TestLoopFixture::TearDown();
  }
};

TEST_F(WeaveConfigManagerTest, InitializeConfigStore) {
  EXPECT_FALSE(files::IsFile(kWeaveConfigStoreTestPath));

  WeaveConfigManager weave_config_manager(kWeaveConfigStoreTestPath);

  std::string file_contents;
  EXPECT_TRUE(files::ReadFileToString(kWeaveConfigStoreTestPath, &file_contents));
  EXPECT_EQ(file_contents, "{}");
}

TEST_F(WeaveConfigManagerTest, InitializeFromExistingConfigStore) {
  constexpr char kTestKeyUint64[] = "test-key-uint64";
  constexpr uint64_t kTestValUint64 = 123456789U;
  constexpr char kTestConfigStoreContents[] = "{\"test-key-uint64\":123456789}";
  EXPECT_FALSE(files::IsFile(kWeaveConfigStoreTestPath));
  EXPECT_TRUE(files::WriteFile(kWeaveConfigStoreTestPath, kTestConfigStoreContents,
                               strlen(kTestConfigStoreContents)));

  WeaveConfigManager weave_config_manager(kWeaveConfigStoreTestPath);

  uint64_t read_value = 0U;
  EXPECT_EQ(weave_config_manager.ReadConfigValue(kTestKeyUint64, &read_value), WEAVE_NO_ERROR);
  EXPECT_EQ(read_value, kTestValUint64);
}

TEST_F(WeaveConfigManagerTest, ReadWriteBool) {
  constexpr char kTestKeyBool[] = "test-key-bool";
  constexpr bool kTestValBool = true;
  constexpr char kTestConfigStoreContents[] = "{\"test-key-bool\":true}";

  EXPECT_EQ(WeaveConfigMgr().WriteConfigValue(kTestKeyBool, kTestValBool), WEAVE_NO_ERROR);

  bool read_value = false;
  EXPECT_EQ(WeaveConfigMgr().ReadConfigValue(kTestKeyBool, &read_value), WEAVE_NO_ERROR);
  EXPECT_EQ(read_value, kTestValBool);

  std::string file_contents;
  EXPECT_TRUE(files::ReadFileToString(kWeaveConfigStoreTestPath, &file_contents));
  EXPECT_EQ(file_contents, kTestConfigStoreContents);
}

TEST_F(WeaveConfigManagerTest, ReadWriteUint) {
  constexpr char kTestKeyUint[] = "test-key-uint";
  constexpr uint32_t kTestValUint = 123456789U;
  constexpr char kTestConfigStoreContents[] = "{\"test-key-uint\":123456789}";

  EXPECT_EQ(WeaveConfigMgr().WriteConfigValue(kTestKeyUint, kTestValUint), WEAVE_NO_ERROR);

  uint32_t read_value = 0U;
  EXPECT_EQ(WeaveConfigMgr().ReadConfigValue(kTestKeyUint, &read_value), WEAVE_NO_ERROR);
  EXPECT_EQ(read_value, kTestValUint);

  std::string file_contents;
  EXPECT_TRUE(files::ReadFileToString(kWeaveConfigStoreTestPath, &file_contents));
  EXPECT_EQ(file_contents, kTestConfigStoreContents);
}

TEST_F(WeaveConfigManagerTest, ReadWriteUint64) {
  constexpr char kTestKeyUint64[] = "test-key-uint64";
  constexpr uint64_t kTestValUint64 = 123456789U;
  constexpr char kTestConfigStoreContents[] = "{\"test-key-uint64\":123456789}";

  EXPECT_EQ(WeaveConfigMgr().WriteConfigValue(kTestKeyUint64, kTestValUint64), WEAVE_NO_ERROR);

  uint64_t read_value = 0U;
  EXPECT_EQ(WeaveConfigMgr().ReadConfigValue(kTestKeyUint64, &read_value), WEAVE_NO_ERROR);
  EXPECT_EQ(read_value, kTestValUint64);

  std::string file_contents;
  EXPECT_TRUE(files::ReadFileToString(kWeaveConfigStoreTestPath, &file_contents));
  EXPECT_EQ(file_contents, kTestConfigStoreContents);
}

TEST_F(WeaveConfigManagerTest, ReadWriteString) {
  constexpr char kTestKeyString[] = "test-key-string";
  constexpr char kTestValString[] = "test-val-string";
  constexpr size_t kTestValStringSize = sizeof(kTestValString);
  constexpr char kTestConfigStoreContents[] = "{\"test-key-string\":\"test-val-string\\u0000\"}";

  EXPECT_EQ(
      WeaveConfigMgr().WriteConfigValueStr(kTestKeyString, kTestValString, kTestValStringSize),
      WEAVE_NO_ERROR);

  char read_value[256];
  size_t read_value_size = 0;
  EXPECT_EQ(WeaveConfigMgr().ReadConfigValueStr(kTestKeyString, read_value, kTestValStringSize,
                                                &read_value_size),
            WEAVE_NO_ERROR);
  EXPECT_EQ(read_value_size, kTestValStringSize);
  EXPECT_EQ(memcmp(kTestValString, read_value, read_value_size), 0);

  EXPECT_EQ(WeaveConfigMgr().ReadConfigValueStr(kTestKeyString, read_value, 0, &read_value_size),
            WEAVE_ERROR_BUFFER_TOO_SMALL);

  std::string file_contents;
  EXPECT_TRUE(files::ReadFileToString(kWeaveConfigStoreTestPath, &file_contents));
  EXPECT_EQ(file_contents, kTestConfigStoreContents);
}

TEST_F(WeaveConfigManagerTest, ReadWriteBinary) {
  constexpr char kTestKeyBinary[] = "test-key-binary";
  constexpr uint8_t kTestValBinary[] = {0xDE, 0xAD, 0xBE, 0xEF};
  constexpr size_t kTestValBinarySize = sizeof(kTestValBinary);
  constexpr char kTestConfigStoreContents[] = "{\"test-key-binary\":\"3q2+7w==\"}";

  EXPECT_EQ(
      WeaveConfigMgr().WriteConfigValueBin(kTestKeyBinary, kTestValBinary, kTestValBinarySize),
      WEAVE_NO_ERROR);

  uint8_t read_value[256];
  size_t read_value_size = 0;
  EXPECT_EQ(WeaveConfigMgr().ReadConfigValueBin(kTestKeyBinary, read_value, kTestValBinarySize,
                                                &read_value_size),
            WEAVE_NO_ERROR);
  EXPECT_EQ(read_value_size, kTestValBinarySize);
  EXPECT_EQ(memcmp(kTestValBinary, read_value, read_value_size), 0);

  EXPECT_EQ(WeaveConfigMgr().ReadConfigValueBin(kTestKeyBinary, read_value, 0, &read_value_size),
            WEAVE_ERROR_BUFFER_TOO_SMALL);

  std::string file_contents;
  EXPECT_TRUE(files::ReadFileToString(kWeaveConfigStoreTestPath, &file_contents));
  EXPECT_EQ(file_contents, kTestConfigStoreContents);
}

TEST_F(WeaveConfigManagerTest, Erasure) {
  constexpr char kTestKeyBool[] = "test-key-bool";
  constexpr char kTestKeyNonExistent[] = "test-nonexistent";
  constexpr bool kTestValBool = true;

  bool read_value = false;
  WeaveConfigMgr().WriteConfigValue(kTestKeyBool, kTestValBool);
  WeaveConfigMgr().ReadConfigValue(kTestKeyBool, &read_value);

  EXPECT_EQ(WeaveConfigMgr().ClearConfigValue(kTestKeyBool), WEAVE_NO_ERROR);
  EXPECT_EQ(WeaveConfigMgr().ReadConfigValue(kTestKeyBool, &read_value),
            WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);
  EXPECT_EQ(WeaveConfigMgr().ClearConfigValue(kTestKeyNonExistent), WEAVE_NO_ERROR);

  std::string file_contents;
  EXPECT_TRUE(files::ReadFileToString(kWeaveConfigStoreTestPath, &file_contents));
  EXPECT_EQ(file_contents, "{}");
}

TEST_F(WeaveConfigManagerTest, Existence) {
  constexpr char kTestKeyBool[] = "test-key-bool";
  constexpr bool kTestValBool = true;

  EXPECT_FALSE(WeaveConfigMgr().ConfigValueExists(kTestKeyBool));
  WeaveConfigMgr().WriteConfigValue(kTestKeyBool, kTestValBool);
  EXPECT_TRUE(WeaveConfigMgr().ConfigValueExists(kTestKeyBool));
  WeaveConfigMgr().ClearConfigValue(kTestKeyBool);
  EXPECT_FALSE(WeaveConfigMgr().ConfigValueExists(kTestKeyBool));
}

TEST_F(WeaveConfigManagerTest, FactoryReset) {
  constexpr char kTestKeyA[] = "test-key-a";
  constexpr char kTestKeyB[] = "test-key-b";
  constexpr bool kTestValBool = true;

  WeaveConfigMgr().WriteConfigValue(kTestKeyA, kTestValBool);
  WeaveConfigMgr().WriteConfigValue(kTestKeyB, kTestValBool);
  EXPECT_TRUE(WeaveConfigMgr().ConfigValueExists(kTestKeyA));
  EXPECT_TRUE(WeaveConfigMgr().ConfigValueExists(kTestKeyB));

  EXPECT_EQ(WeaveConfigMgr().FactoryResetConfig(), WEAVE_NO_ERROR);

  EXPECT_FALSE(WeaveConfigMgr().ConfigValueExists(kTestKeyA));
  EXPECT_FALSE(WeaveConfigMgr().ConfigValueExists(kTestKeyB));

  bool read_value = false;
  EXPECT_EQ(WeaveConfigMgr().ReadConfigValue(kTestKeyA, &read_value),
            WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);
  EXPECT_EQ(WeaveConfigMgr().ReadConfigValue(kTestKeyB, &read_value),
            WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);

  std::string file_contents;
  EXPECT_TRUE(files::ReadFileToString(kWeaveConfigStoreTestPath, &file_contents));
  EXPECT_EQ(file_contents, "{}");
}

TEST_F(WeaveConfigManagerTest, Consistency) {
  constexpr char kTestKeyBool[] = "test-key-bool";
  constexpr bool kTestValBool = true;

  WeaveConfigMgr().WriteConfigValue(kTestKeyBool, kTestValBool);

  bool read_value = false;
  EXPECT_EQ(WeaveConfigMgr().ReadConfigValue(kTestKeyBool, &read_value), WEAVE_NO_ERROR);
  EXPECT_EQ(read_value, kTestValBool);
  // Check the first key again. The underlying library used to marshal JSON
  // prefers move semantics, so verify that reading a variable out of the store
  // does not invalidate its state.
  EXPECT_EQ(WeaveConfigMgr().ReadConfigValue(kTestKeyBool, &read_value), WEAVE_NO_ERROR);
  EXPECT_EQ(read_value, kTestValBool);
}

TEST_F(WeaveConfigManagerTest, FailOnMismatchedTypes) {
  constexpr char kTestKeyBool[] = "test-key-bool";
  constexpr char kTestKeyUint[] = "test-key-uint";
  constexpr char kTestKeyUint64[] = "test-key-uint64";
  constexpr char kTestKeyString[] = "test-key-string";
  // Binary values are intentionally ignored, as they are stored as strings.
  constexpr bool kTestValBool = true;
  constexpr uint32_t kTestValUint = 12345U;
  constexpr uint64_t kTestValUint64 = 123456789U;
  constexpr char kTestValString[] = "test-string-val";

  WeaveConfigMgr().WriteConfigValue(kTestKeyBool, kTestValBool);
  WeaveConfigMgr().WriteConfigValue(kTestKeyUint, kTestValUint);
  WeaveConfigMgr().WriteConfigValue(kTestKeyUint64, kTestValUint64);
  WeaveConfigMgr().WriteConfigValueStr(kTestKeyString, kTestValString, sizeof(kTestValString));

  bool read_bool_value = false;
  uint32_t read_uint_value = 0U;
  uint64_t read_uint64_value = 0U;
  char read_string_value[256];
  size_t read_string_value_size = 0;
  // Every key is mapped to an incompatible type.
  EXPECT_EQ(WeaveConfigMgr().ReadConfigValue(kTestKeyBool, &read_uint_value),
            WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);
  EXPECT_EQ(WeaveConfigMgr().ReadConfigValue(kTestKeyUint, &read_bool_value),
            WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);
  EXPECT_EQ(WeaveConfigMgr().ReadConfigValueStr(kTestKeyUint64, read_string_value,
                                                sizeof(read_string_value), &read_string_value_size),
            WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);
  EXPECT_EQ(WeaveConfigMgr().ReadConfigValue(kTestKeyString, &read_uint64_value),
            WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);
}

}  // namespace testing
}  // namespace nl::Weave::DeviceLayer::Internal
