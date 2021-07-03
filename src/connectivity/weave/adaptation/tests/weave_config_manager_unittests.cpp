// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/gtest/test_loop_fixture.h>
#include <string.h>

#include <gtest/gtest.h>

#include "src/connectivity/weave/adaptation/weave_config_manager.h"
#include "src/connectivity/weave/adaptation/weave_device_platform_error.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"

namespace nl::Weave::DeviceLayer::Internal::testing {
namespace {
constexpr char kWeaveConfigStoreTestPath[] = "/data/config.json";
constexpr char kWeaveConfigStoreReadOnlyTestPath[] = "/pkg/data/config.json";
constexpr char kWeaveConfigStoreAltTestPath[] = "/data/alt.config.json";

constexpr char kWeaveConfigDefaultStorePath[] = "/pkg/data/default_store.json";
constexpr char kWeaveConfigInvalidDefaultStorePath[] = "/pkg/data/invalid_default_store.json";
constexpr char kWeaveConfigDefaultStoreSchemaPath[] = "/pkg/data/default_store_schema.json";
constexpr char kDeviceInfoPath[] = "/config/data/device_info.json";
constexpr char kDeviceInfoSchemaPath[] = "/pkg/data/device_info_schema.json";
}  // namespace

class WeaveConfigManagerTest : public ::gtest::TestLoopFixture {
 public:
  WeaveConfigManagerTest() : weave_config_manager_(kWeaveConfigStoreTestPath) {}

  void TearDown() override {
    EXPECT_EQ(WeaveConfigMgr().FactoryResetConfig(), WEAVE_NO_ERROR);
    EXPECT_TRUE(files::DeletePath(kWeaveConfigStoreTestPath, false));
    TestLoopFixture::TearDown();
  }

  WeaveConfigManager& weave_config_manager() { return weave_config_manager_; }

 private:
  WeaveConfigManager weave_config_manager_;
};

TEST_F(WeaveConfigManagerTest, InitializeFromExistingConfigStore) {
  constexpr char kTestKeyUint64[] = "test-key-uint64";
  constexpr uint64_t kTestValUint64 = 123456789U;
  constexpr char kTestConfigStoreContents[] = "{\"test-key-uint64\":123456789}";
  EXPECT_FALSE(files::IsFile(kWeaveConfigStoreAltTestPath));
  EXPECT_TRUE(files::WriteFile(kWeaveConfigStoreAltTestPath, kTestConfigStoreContents,
                               strlen(kTestConfigStoreContents)));

  WeaveConfigManager weave_config_manager(kWeaveConfigStoreAltTestPath);

  uint64_t read_value = 0U;
  EXPECT_EQ(weave_config_manager.ReadConfigValue(kTestKeyUint64, &read_value), WEAVE_NO_ERROR);
  EXPECT_EQ(read_value, kTestValUint64);

  EXPECT_TRUE(files::DeletePath(kWeaveConfigStoreAltTestPath, false));
}

TEST_F(WeaveConfigManagerTest, ReadWriteBool) {
  constexpr char kTestKeyBool[] = "test-key-bool";
  constexpr bool kTestValBool = true;
  constexpr char kTestConfigStoreContents[] = "{\"test-key-bool\":true}";

  EXPECT_EQ(weave_config_manager().WriteConfigValue(kTestKeyBool, kTestValBool), WEAVE_NO_ERROR);

  bool read_value = false;
  EXPECT_EQ(weave_config_manager().ReadConfigValue(kTestKeyBool, &read_value), WEAVE_NO_ERROR);
  EXPECT_EQ(read_value, kTestValBool);

  std::string file_contents;
  EXPECT_TRUE(files::ReadFileToString(kWeaveConfigStoreTestPath, &file_contents));
  EXPECT_EQ(file_contents, kTestConfigStoreContents);
}

TEST_F(WeaveConfigManagerTest, ReadWriteUint) {
  constexpr char kTestKeyUint[] = "test-key-uint";
  constexpr uint32_t kTestValUint = 123456789U;
  constexpr char kTestConfigStoreContents[] = "{\"test-key-uint\":123456789}";

  EXPECT_EQ(weave_config_manager().WriteConfigValue(kTestKeyUint, kTestValUint), WEAVE_NO_ERROR);

  uint32_t read_value = 0U;
  EXPECT_EQ(weave_config_manager().ReadConfigValue(kTestKeyUint, &read_value), WEAVE_NO_ERROR);
  EXPECT_EQ(read_value, kTestValUint);

  std::string file_contents;
  EXPECT_TRUE(files::ReadFileToString(kWeaveConfigStoreTestPath, &file_contents));
  EXPECT_EQ(file_contents, kTestConfigStoreContents);
}

TEST_F(WeaveConfigManagerTest, ReadUint16InvalidSize) {
  constexpr char kTestKeyUint16[] = "test-key-uint16";
  constexpr uint32_t kTestValUint = UINT16_MAX + 1;
  constexpr char kTestConfigStoreContents[] = "{\"test-key-uint16\":65536}";

  EXPECT_EQ(weave_config_manager().WriteConfigValue(kTestKeyUint16, kTestValUint), WEAVE_NO_ERROR);

  uint16_t read_value = 0U;
  EXPECT_EQ(weave_config_manager().ReadConfigValue(kTestKeyUint16, &read_value),
            WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);

  std::string file_contents;
  EXPECT_TRUE(files::ReadFileToString(kWeaveConfigStoreTestPath, &file_contents));
  EXPECT_EQ(file_contents, kTestConfigStoreContents);
}

TEST_F(WeaveConfigManagerTest, ReadWriteUint64) {
  constexpr char kTestKeyUint64[] = "test-key-uint64";
  constexpr uint64_t kTestValUint64 = 123456789U;
  constexpr char kTestConfigStoreContents[] = "{\"test-key-uint64\":123456789}";

  EXPECT_EQ(weave_config_manager().WriteConfigValue(kTestKeyUint64, kTestValUint64),
            WEAVE_NO_ERROR);

  uint64_t read_value = 0U;
  EXPECT_EQ(weave_config_manager().ReadConfigValue(kTestKeyUint64, &read_value), WEAVE_NO_ERROR);
  EXPECT_EQ(read_value, kTestValUint64);

  std::string file_contents;
  EXPECT_TRUE(files::ReadFileToString(kWeaveConfigStoreTestPath, &file_contents));
  EXPECT_EQ(file_contents, kTestConfigStoreContents);
}

TEST_F(WeaveConfigManagerTest, ReadFailOnNegativeValues) {
  constexpr char kTestKeyUint[] = "test-key-uint";
  constexpr char kTestKeyUint16[] = "test-key-uint16";
  constexpr char kTestKeyUint64[] = "test-key-uint64";
  constexpr char kTestConfigStoreContents[] =
      "{\"test-key-uint\":-1,\"test-key-uint16\":-1,\"test-key-uint64\":-1}";
  EXPECT_FALSE(files::IsFile(kWeaveConfigStoreAltTestPath));
  EXPECT_TRUE(files::WriteFile(kWeaveConfigStoreAltTestPath, kTestConfigStoreContents,
                               strlen(kTestConfigStoreContents)));

  uint64_t read_value = 0U;
  EXPECT_EQ(weave_config_manager().ReadConfigValue(kTestKeyUint, &read_value),
            WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);
  EXPECT_EQ(weave_config_manager().ReadConfigValue(kTestKeyUint16, &read_value),
            WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);
  EXPECT_EQ(weave_config_manager().ReadConfigValue(kTestKeyUint64, &read_value),
            WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);
  EXPECT_TRUE(files::DeletePath(kWeaveConfigStoreAltTestPath, false));
}

TEST_F(WeaveConfigManagerTest, ReadWriteString) {
  constexpr char kTestKeyString[] = "test-key-string";
  constexpr char kTestValString[] = "test-val-string";
  constexpr size_t kTestValStringSize = sizeof(kTestValString);
  constexpr char kTestConfigStoreContents[] = "{\"test-key-string\":\"test-val-string\\u0000\"}";

  EXPECT_EQ(weave_config_manager().WriteConfigValueStr(kTestKeyString, kTestValString,
                                                       kTestValStringSize),
            WEAVE_NO_ERROR);

  char read_value[256];
  size_t read_value_size = 0;
  std::memset(read_value, 0xFF, sizeof(read_value));
  EXPECT_EQ(weave_config_manager().ReadConfigValueStr(kTestKeyString, nullptr, 0, &read_value_size),
            WEAVE_NO_ERROR);
  EXPECT_EQ(read_value_size, strlen(kTestKeyString));

  // Reset read_value_size to confirm it gets verified again in the following check.
  read_value_size = 0;
  EXPECT_EQ(weave_config_manager().ReadConfigValueStr(kTestKeyString, read_value,
                                                      kTestValStringSize, &read_value_size),
            WEAVE_NO_ERROR);
  EXPECT_EQ(read_value_size, strlen(kTestKeyString));
  EXPECT_EQ(memcmp(kTestValString, read_value, read_value_size), 0);

  EXPECT_EQ(
      weave_config_manager().ReadConfigValueStr(kTestKeyString, read_value, 0, &read_value_size),
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

  EXPECT_EQ(weave_config_manager().WriteConfigValueBin(kTestKeyBinary, kTestValBinary,
                                                       kTestValBinarySize),
            WEAVE_NO_ERROR);

  uint8_t read_value[256];
  size_t read_value_size = 0;
  EXPECT_EQ(weave_config_manager().ReadConfigValueBin(kTestKeyBinary, nullptr, 0, &read_value_size),
            WEAVE_NO_ERROR);
  EXPECT_EQ(read_value_size, kTestValBinarySize);

  // Reset read_value_size to confirm it gets verified again in the following check.
  read_value_size = 0;
  EXPECT_EQ(weave_config_manager().ReadConfigValueBin(kTestKeyBinary, read_value,
                                                      kTestValBinarySize, &read_value_size),
            WEAVE_NO_ERROR);
  EXPECT_EQ(read_value_size, kTestValBinarySize);
  EXPECT_EQ(memcmp(kTestValBinary, read_value, read_value_size), 0);

  EXPECT_EQ(
      weave_config_manager().ReadConfigValueBin(kTestKeyBinary, read_value, 0, &read_value_size),
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
  weave_config_manager().WriteConfigValue(kTestKeyBool, kTestValBool);
  weave_config_manager().ReadConfigValue(kTestKeyBool, &read_value);

  EXPECT_EQ(weave_config_manager().ClearConfigValue(kTestKeyBool), WEAVE_NO_ERROR);
  EXPECT_EQ(weave_config_manager().ReadConfigValue(kTestKeyBool, &read_value),
            WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);
  EXPECT_EQ(weave_config_manager().ClearConfigValue(kTestKeyNonExistent), WEAVE_NO_ERROR);

  std::string file_contents;
  EXPECT_TRUE(files::ReadFileToString(kWeaveConfigStoreTestPath, &file_contents));
  EXPECT_EQ(file_contents, "{}");
}

TEST_F(WeaveConfigManagerTest, Existence) {
  constexpr char kTestKeyBool[] = "test-key-bool";
  constexpr bool kTestValBool = true;

  EXPECT_FALSE(weave_config_manager().ConfigValueExists(kTestKeyBool));
  weave_config_manager().WriteConfigValue(kTestKeyBool, kTestValBool);
  EXPECT_TRUE(weave_config_manager().ConfigValueExists(kTestKeyBool));
  weave_config_manager().ClearConfigValue(kTestKeyBool);
  EXPECT_FALSE(weave_config_manager().ConfigValueExists(kTestKeyBool));
}

TEST_F(WeaveConfigManagerTest, FactoryReset) {
  constexpr char kTestKeyA[] = "test-key-a";
  constexpr char kTestKeyB[] = "test-key-b";
  constexpr bool kTestValBool = true;

  weave_config_manager().WriteConfigValue(kTestKeyA, kTestValBool);
  weave_config_manager().WriteConfigValue(kTestKeyB, kTestValBool);
  EXPECT_TRUE(weave_config_manager().ConfigValueExists(kTestKeyA));
  EXPECT_TRUE(weave_config_manager().ConfigValueExists(kTestKeyB));

  EXPECT_EQ(weave_config_manager().FactoryResetConfig(), WEAVE_NO_ERROR);

  EXPECT_FALSE(weave_config_manager().ConfigValueExists(kTestKeyA));
  EXPECT_FALSE(weave_config_manager().ConfigValueExists(kTestKeyB));

  bool read_value = false;
  EXPECT_EQ(weave_config_manager().ReadConfigValue(kTestKeyA, &read_value),
            WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);
  EXPECT_EQ(weave_config_manager().ReadConfigValue(kTestKeyB, &read_value),
            WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);

  std::string file_contents;
  EXPECT_TRUE(files::ReadFileToString(kWeaveConfigStoreTestPath, &file_contents));
  EXPECT_EQ(file_contents, "{}");
}

TEST_F(WeaveConfigManagerTest, Consistency) {
  constexpr char kTestKeyBool[] = "test-key-bool";
  constexpr bool kTestValBool = true;

  weave_config_manager().WriteConfigValue(kTestKeyBool, kTestValBool);

  bool read_value = false;
  EXPECT_EQ(weave_config_manager().ReadConfigValue(kTestKeyBool, &read_value), WEAVE_NO_ERROR);
  EXPECT_EQ(read_value, kTestValBool);
  // Check the first key again. The underlying library used to marshal JSON
  // prefers move semantics, so verify that reading a variable out of the store
  // does not invalidate its state.
  EXPECT_EQ(weave_config_manager().ReadConfigValue(kTestKeyBool, &read_value), WEAVE_NO_ERROR);
  EXPECT_EQ(read_value, kTestValBool);
}

TEST_F(WeaveConfigManagerTest, FailOnMismatchedTypes) {
  constexpr char kTestKeyBool[] = "test-key-bool";
  constexpr char kTestKeyUint[] = "test-key-uint";
  constexpr char kTestKeyUint64[] = "test-key-uint64";
  constexpr char kTestKeyString[] = "test-key-string";
  constexpr char kTestKeyArray[] = "test-key-array";
  // Binary values are intentionally ignored, as they are stored as strings.
  constexpr bool kTestValBool = true;
  constexpr uint32_t kTestValUint = 12345U;
  constexpr uint64_t kTestValUint64 = 123456789U;
  constexpr char kTestValString[] = "test-string-val";
  std::vector<std::string> kTestValArray = {"test1", "test2"};

  weave_config_manager().WriteConfigValue(kTestKeyBool, kTestValBool);
  weave_config_manager().WriteConfigValue(kTestKeyUint, kTestValUint);
  weave_config_manager().WriteConfigValue(kTestKeyUint64, kTestValUint64);
  weave_config_manager().WriteConfigValueStr(kTestKeyString, kTestValString,
                                             sizeof(kTestValString));
  weave_config_manager().WriteConfigValueArray(kTestKeyArray, kTestValArray);

  bool read_bool_value = false;
  uint32_t read_uint_value = 0U;
  uint64_t read_uint64_value = 0U;
  char read_string_value[256];
  size_t read_string_value_size = 0;
  std::vector<std::string> read_array_value;

  // Every key is mapped to an incompatible type.
  EXPECT_EQ(weave_config_manager().ReadConfigValue(kTestKeyBool, &read_uint_value),
            WEAVE_DEVICE_PLATFORM_ERROR_CONFIG_TYPE_MISMATCH);
  EXPECT_EQ(weave_config_manager().ReadConfigValue(kTestKeyUint, &read_bool_value),
            WEAVE_DEVICE_PLATFORM_ERROR_CONFIG_TYPE_MISMATCH);
  EXPECT_EQ(
      weave_config_manager().ReadConfigValueStr(kTestKeyUint64, read_string_value,
                                                sizeof(read_string_value), &read_string_value_size),
      WEAVE_DEVICE_PLATFORM_ERROR_CONFIG_TYPE_MISMATCH);
  EXPECT_EQ(weave_config_manager().ReadConfigValue(kTestKeyString, &read_uint64_value),
            WEAVE_DEVICE_PLATFORM_ERROR_CONFIG_TYPE_MISMATCH);
  EXPECT_EQ(weave_config_manager().ReadConfigValueArray(kTestKeyUint64, read_array_value),
            WEAVE_DEVICE_PLATFORM_ERROR_CONFIG_TYPE_MISMATCH);
}

TEST_F(WeaveConfigManagerTest, OverwriteKeys) {
  constexpr char kTestKeyBool[] = "test-key-bool";
  constexpr bool kTestValBool = true;
  constexpr char kTestConfigStoreContents[] = "{\"test-key-bool\":true}";

  EXPECT_EQ(weave_config_manager().WriteConfigValue(kTestKeyBool, !kTestValBool), WEAVE_NO_ERROR);
  EXPECT_EQ(weave_config_manager().WriteConfigValue(kTestKeyBool, kTestValBool), WEAVE_NO_ERROR);

  bool read_value = false;
  EXPECT_EQ(weave_config_manager().ReadConfigValue(kTestKeyBool, &read_value), WEAVE_NO_ERROR);
  EXPECT_EQ(read_value, kTestValBool);

  std::string file_contents;
  EXPECT_TRUE(files::ReadFileToString(kWeaveConfigStoreTestPath, &file_contents));
  EXPECT_EQ(file_contents, kTestConfigStoreContents);
}

TEST_F(WeaveConfigManagerTest, ReadOnly) {
  constexpr char kTestKeyBool[] = "test-key-bool";
  constexpr bool kTestValBool = true;
  std::unique_ptr<WeaveConfigReader> reader =
      WeaveConfigManager::CreateInstance(kWeaveConfigStoreReadOnlyTestPath);

  bool read_value = false;
  EXPECT_EQ(reader->ReadConfigValue(kTestKeyBool, &read_value), WEAVE_NO_ERROR);
  EXPECT_EQ(read_value, kTestValBool);
}

TEST_F(WeaveConfigManagerTest, SetDefaultConfiguration) {
  constexpr char kTestKeyBool[] = "test-key-bool";
  constexpr bool kTestValBool = true;
  constexpr char kTestConfigStoreContents[] = "{\"test-key-bool\":true,\"vendor-id\":1234}";

  // From the factory reset state, setting the default configuration should
  // reflect immediately after a read.
  EXPECT_EQ(weave_config_manager().SetConfiguration(kWeaveConfigDefaultStorePath,
                                                    kWeaveConfigDefaultStoreSchemaPath,
                                                    /*should_replace*/ false),
            WEAVE_NO_ERROR);

  bool read_value = true;
  EXPECT_EQ(weave_config_manager().ReadConfigValue(kTestKeyBool, &read_value), WEAVE_NO_ERROR);
  EXPECT_EQ(read_value, false);

  read_value = false;
  EXPECT_EQ(weave_config_manager().WriteConfigValue(kTestKeyBool, kTestValBool), WEAVE_NO_ERROR);
  EXPECT_EQ(weave_config_manager().ReadConfigValue(kTestKeyBool, &read_value), WEAVE_NO_ERROR);
  EXPECT_EQ(read_value, kTestValBool);

  // If a value already exists in the configuration, setting the default
  // configuration does not modify it.
  EXPECT_EQ(weave_config_manager().SetConfiguration(kWeaveConfigDefaultStorePath,
                                                    kWeaveConfigDefaultStoreSchemaPath,
                                                    /*should_replace*/ false),
            WEAVE_NO_ERROR);

  read_value = false;
  EXPECT_EQ(weave_config_manager().ReadConfigValue(kTestKeyBool, &read_value), WEAVE_NO_ERROR);
  EXPECT_EQ(read_value, kTestValBool);

  std::string file_contents;
  EXPECT_TRUE(files::ReadFileToString(kWeaveConfigStoreTestPath, &file_contents));
  EXPECT_EQ(file_contents, kTestConfigStoreContents);
}

TEST_F(WeaveConfigManagerTest, SetDefaultConfigurationMissingFiles) {
  constexpr char kInvalidPath[] = "/pkg/data/missing_file.json";
  EXPECT_EQ(
      weave_config_manager().SetConfiguration(kInvalidPath, kWeaveConfigDefaultStoreSchemaPath,
                                              /*should_replace*/ false),
      WEAVE_ERROR_PERSISTED_STORAGE_FAIL);
  EXPECT_EQ(weave_config_manager().SetConfiguration(kWeaveConfigDefaultStorePath, kInvalidPath,
                                                    /*should_replace*/ false),
            WEAVE_ERROR_PERSISTED_STORAGE_FAIL);
}

TEST_F(WeaveConfigManagerTest, SetDefaultConfigurationInvalidConfig) {
  EXPECT_EQ(weave_config_manager().SetConfiguration(kWeaveConfigInvalidDefaultStorePath,
                                                    kWeaveConfigDefaultStoreSchemaPath,
                                                    /*should_replace*/ false),
            WEAVE_DEVICE_PLATFORM_ERROR_CONFIG_INVALID);
}

TEST_F(WeaveConfigManagerTest, SetDefaultConfigurationReplace) {
  constexpr char kTestKeyBool[] = "test-key-bool";
  constexpr char kTestSerialNumber[] = "serial-number";
  constexpr char kTestSerialNumberVal[] = "ABCD1234";
  constexpr size_t kTestSerialNumberValSize = sizeof(kTestSerialNumberVal);
  constexpr char kTestKeyVendorId[] = "vendor-id";
  constexpr uint16_t kTestVendorIdVal1 = 1234;
  constexpr uint16_t kTestVendorIdVal2 = 5050;

  // From the factory reset state, setting the default configuration should
  // reflect immediately after a read.
  EXPECT_EQ(weave_config_manager().SetConfiguration(kWeaveConfigDefaultStorePath,
                                                    kWeaveConfigDefaultStoreSchemaPath,
                                                    /*should_replace*/ false),
            WEAVE_NO_ERROR);

  bool read_value = true;
  EXPECT_EQ(weave_config_manager().ReadConfigValue(kTestKeyBool, &read_value), WEAVE_NO_ERROR);
  EXPECT_EQ(read_value, false);

  uint16_t vendor_id = 0;
  EXPECT_EQ(weave_config_manager().ReadConfigValue(kTestKeyVendorId, &vendor_id), WEAVE_NO_ERROR);
  EXPECT_EQ(vendor_id, kTestVendorIdVal1);

  // Replace the existing configuration.
  EXPECT_EQ(weave_config_manager().SetConfiguration(kDeviceInfoPath, kDeviceInfoSchemaPath,
                                                    /*should_replace*/ true),
            WEAVE_NO_ERROR);

  char read_value_str[256] = {'\0'};
  size_t read_value_size = 0;
  read_value = true;
  EXPECT_EQ(weave_config_manager().ReadConfigValue(kTestKeyBool, &read_value), WEAVE_NO_ERROR);
  EXPECT_EQ(read_value, false);
  EXPECT_EQ(weave_config_manager().ReadConfigValueStr(kTestSerialNumber, read_value_str,
                                                      kTestSerialNumberValSize, &read_value_size),
            WEAVE_NO_ERROR);
  EXPECT_STREQ(kTestSerialNumberVal, read_value_str);

  EXPECT_EQ(weave_config_manager().ReadConfigValue(kTestKeyVendorId, &vendor_id), WEAVE_NO_ERROR);
  EXPECT_EQ(vendor_id, kTestVendorIdVal2);
}

TEST_F(WeaveConfigManagerTest, ReadArray) {
  constexpr char kTestKeyArray[] = "applets";
  std::vector<std::string> write_value = {"applet1", "applet2", "applet3"};
  std::vector<std::string> read_value;

  EXPECT_EQ(weave_config_manager().WriteConfigValueArray(kTestKeyArray, write_value),
            WEAVE_NO_ERROR);
  EXPECT_EQ(weave_config_manager().ReadConfigValueArray(kTestKeyArray, read_value), WEAVE_NO_ERROR);

  EXPECT_EQ(write_value.size(), read_value.size());

  for (size_t i = 0; i < read_value.size(); i++) {
    EXPECT_EQ(write_value[i], read_value[i]);
  }
}

TEST_F(WeaveConfigManagerTest, ReadInvalidArray) {
  constexpr char kTestKeyApplets[] = "applets";
  constexpr char kTestConfigStoreContents[] = "{\"applets\":[1234567]}";
  std::vector<std::string> read_value;
  EXPECT_FALSE(files::IsFile(kWeaveConfigStoreAltTestPath));
  EXPECT_TRUE(files::WriteFile(kWeaveConfigStoreAltTestPath, kTestConfigStoreContents,
                               strlen(kTestConfigStoreContents)));

  WeaveConfigManager weave_config_manager(kWeaveConfigStoreAltTestPath);
  EXPECT_EQ(weave_config_manager.ReadConfigValueArray(kTestKeyApplets, read_value),
            WEAVE_DEVICE_PLATFORM_ERROR_CONFIG_TYPE_MISMATCH);
  EXPECT_TRUE(files::DeletePath(kWeaveConfigStoreAltTestPath, false));
}

}  // namespace nl::Weave::DeviceLayer::Internal::testing
