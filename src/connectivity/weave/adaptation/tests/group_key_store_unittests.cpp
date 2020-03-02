// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include "group_key_store_impl.h"
// clang-format on

#include <lib/gtest/test_loop_fixture.h>
#include "src/lib/files/file.h"
#include "gtest/gtest.h"
#include "fuchsia_config.h"

namespace adaptation {
namespace testing {
namespace {
using nl::Weave::WeaveKeyId;
using nl::Weave::DeviceLayer::ConfigurationMgr;
using nl::Weave::DeviceLayer::Internal::FuchsiaConfig;
using nl::Weave::DeviceLayer::Internal::GroupKeyStoreImpl;
using nl::Weave::Profiles::Security::AppKeys::WeaveGroupKey;
constexpr uint32_t kTestKeyId = WeaveKeyId::kFabricSecret + 1u;
constexpr uint8_t kWeaveAppGroupKeySize =
    nl::Weave::Profiles::Security::AppKeys::kWeaveAppGroupKeySize;
constexpr uint8_t kMaxGroupKeys =
    nl::Weave::DeviceLayer::Internal::GroupKeyStoreImpl::kMaxGroupKeys;
constexpr char kWeaveConfigStoreTestPath[] = "/data/config.json";
}  // namespace

class GroupKeyStoreTest : public ::gtest::TestLoopFixture {
 public:
  GroupKeyStoreTest() {}

  void SetUp() override {
    TestLoopFixture::SetUp();
    EXPECT_EQ(group_key_store_.Init(), WEAVE_NO_ERROR);
  }

  void TearDown() override {
    EXPECT_EQ(FuchsiaConfig::FactoryResetConfig(), WEAVE_NO_ERROR);
    TestLoopFixture::TearDown();
  }

 protected:
  void WriteConfigStore(const char* config_data) {
    EXPECT_EQ(FuchsiaConfig::FactoryResetConfig(), WEAVE_NO_ERROR);
    EXPECT_TRUE(files::WriteFile(kWeaveConfigStoreTestPath, config_data, strlen(config_data)));
  }

  std::string ReadConfigStore() {
    std::string file_contents;
    EXPECT_TRUE(files::ReadFileToString(kWeaveConfigStoreTestPath, &file_contents));
    return file_contents;
  }

  WeaveGroupKey CreateGroupKey(uint32_t key_id, uint8_t key_byte = 0,
                               uint8_t key_len = kWeaveAppGroupKeySize, uint32_t start_time = 0) {
    WeaveGroupKey group_key{
        .KeyId = key_id,
        .KeyLen = key_len,
        .StartTime = start_time,
    };
    memset(group_key.Key, 0, sizeof(group_key.Key));
    memset(group_key.Key, key_byte, key_len);
    return group_key;
  }

  void VerifyGroupKey(const WeaveGroupKey& key, const WeaveGroupKey& key_expected) {
    EXPECT_EQ(key.KeyId, key_expected.KeyId);
    EXPECT_EQ(key.KeyLen, key_expected.KeyLen);
    EXPECT_EQ(memcmp(key.Key, key_expected.Key, sizeof(key_expected.Key)), 0);
    if (key_expected.KeyId != WeaveKeyId::kFabricSecret) {
      EXPECT_EQ(key.StartTime, key_expected.StartTime);
    }
  }

  GroupKeyStoreImpl group_key_store_;
};

TEST_F(GroupKeyStoreTest, Initialize) { EXPECT_EQ(ReadConfigStore(), "{}"); }

TEST_F(GroupKeyStoreTest, InitializeWithExistingStore) {
  uint32_t key_ids[kMaxGroupKeys];
  uint8_t key_count;

  // Sample valid key set.
  uint32_t test_key_ids[1] = {kTestKeyId};
  uint8_t test_key_data[kWeaveAppGroupKeySize];
  EXPECT_EQ(FuchsiaConfig::WriteConfigValueBin(FuchsiaConfig::kConfigKey_GroupKeyIndex,
                                               (uint8_t*)test_key_ids, sizeof(test_key_ids)),
            WEAVE_NO_ERROR);
  EXPECT_EQ(FuchsiaConfig::WriteConfigValueBin("gk-00001002", test_key_data, sizeof(test_key_data)),
            WEAVE_NO_ERROR);
  EXPECT_EQ(group_key_store_.Init(), WEAVE_NO_ERROR);
  EXPECT_EQ(
      group_key_store_.EnumerateGroupKeys(WeaveKeyId::kNone, key_ids, kMaxGroupKeys, key_count),
      WEAVE_NO_ERROR);
  EXPECT_EQ(key_count, 1);
}

TEST_F(GroupKeyStoreTest, StoreAndRetrieveKey) {
  const WeaveGroupKey test_key = CreateGroupKey(kTestKeyId, 0, kWeaveAppGroupKeySize, 0xABCDEF);
  EXPECT_EQ(group_key_store_.StoreGroupKey(test_key), WEAVE_NO_ERROR);

  WeaveGroupKey retrieved_key;
  EXPECT_EQ(group_key_store_.RetrieveGroupKey(kTestKeyId, retrieved_key), WEAVE_NO_ERROR);
  VerifyGroupKey(retrieved_key, test_key);
}

TEST_F(GroupKeyStoreTest, StoreAndRetrieveFabricSecretKey) {
  const WeaveGroupKey test_key =
      CreateGroupKey(WeaveKeyId::kFabricSecret, 0, kWeaveAppGroupKeySize);
  EXPECT_EQ(group_key_store_.StoreGroupKey(test_key), WEAVE_NO_ERROR);

  WeaveGroupKey retrieved_key;
  EXPECT_EQ(group_key_store_.RetrieveGroupKey(WeaveKeyId::kFabricSecret, retrieved_key),
            WEAVE_NO_ERROR);
  VerifyGroupKey(retrieved_key, test_key);
}

TEST_F(GroupKeyStoreTest, RetrieveNonExistentKey) {
  WeaveGroupKey retrieved_key;
  EXPECT_EQ(group_key_store_.RetrieveGroupKey(kTestKeyId, retrieved_key),
            WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);

  const WeaveGroupKey test_key = CreateGroupKey(kTestKeyId + 1, 0, kWeaveAppGroupKeySize, 12345U);
  EXPECT_EQ(group_key_store_.StoreGroupKey(test_key), WEAVE_NO_ERROR);
  EXPECT_EQ(group_key_store_.RetrieveGroupKey(kTestKeyId, retrieved_key),
            WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);
}

TEST_F(GroupKeyStoreTest, DeleteKey) {
  const WeaveGroupKey test_key = CreateGroupKey(kTestKeyId, 0, kWeaveAppGroupKeySize, 0xABCDEF);
  EXPECT_EQ(group_key_store_.StoreGroupKey(test_key), WEAVE_NO_ERROR);

  WeaveGroupKey retrieved_key;
  EXPECT_EQ(group_key_store_.RetrieveGroupKey(kTestKeyId, retrieved_key), WEAVE_NO_ERROR);
  EXPECT_EQ(group_key_store_.DeleteGroupKey(kTestKeyId), WEAVE_NO_ERROR);
}

TEST_F(GroupKeyStoreTest, DeleteNonExistentKey) {
  EXPECT_EQ(group_key_store_.DeleteGroupKey(kTestKeyId), WEAVE_ERROR_KEY_NOT_FOUND);
}

TEST_F(GroupKeyStoreTest, EnumerateKeys) {
  const uint32_t none_key_id = WeaveKeyId::kType_None + 1;
  const uint32_t general_key_id = WeaveKeyId::kType_General + 1;
  const uint32_t rotating_key_id = WeaveKeyId::kType_AppRotatingKey + 1;
  const WeaveGroupKey none_key = CreateGroupKey(none_key_id, 0, kWeaveAppGroupKeySize);
  const WeaveGroupKey general_key = CreateGroupKey(general_key_id, 0, kWeaveAppGroupKeySize);
  const WeaveGroupKey rotating_key = CreateGroupKey(rotating_key_id, 0, kWeaveAppGroupKeySize);

  EXPECT_EQ(group_key_store_.StoreGroupKey(none_key), WEAVE_NO_ERROR);
  EXPECT_EQ(group_key_store_.StoreGroupKey(general_key), WEAVE_NO_ERROR);
  EXPECT_EQ(group_key_store_.StoreGroupKey(rotating_key), WEAVE_NO_ERROR);

  uint32_t key_ids[kMaxGroupKeys];
  uint8_t key_count;
  // Verify that WeaveKeyId::kNone returns all keys.
  EXPECT_EQ(
      group_key_store_.EnumerateGroupKeys(WeaveKeyId::kNone, key_ids, kMaxGroupKeys, key_count),
      WEAVE_NO_ERROR);
  EXPECT_EQ(key_count, 3);
  EXPECT_NE(std::find(std::begin(key_ids), std::end(key_ids), none_key_id), std::end(key_ids));
  EXPECT_NE(std::find(std::begin(key_ids), std::end(key_ids), general_key_id), std::end(key_ids));
  EXPECT_NE(std::find(std::begin(key_ids), std::end(key_ids), rotating_key_id), std::end(key_ids));
  // Verify that only the general key is returned.
  EXPECT_EQ(group_key_store_.EnumerateGroupKeys(WeaveKeyId::kType_General, key_ids, kMaxGroupKeys,
                                                key_count),
            WEAVE_NO_ERROR);
  EXPECT_EQ(key_count, 1);
  EXPECT_EQ(key_ids[0], general_key_id);
  // Verify that only the rotating key is returned.
  EXPECT_EQ(group_key_store_.EnumerateGroupKeys(WeaveKeyId::kType_AppRotatingKey, key_ids,
                                                kMaxGroupKeys, key_count),
            WEAVE_NO_ERROR);
  EXPECT_EQ(key_count, 1);
  EXPECT_EQ(key_ids[0], rotating_key_id);
  // Verify that no keys are returned if the type doesn't exist.
  EXPECT_EQ(group_key_store_.EnumerateGroupKeys(WeaveKeyId::kType_AppRootKey, key_ids,
                                                kMaxGroupKeys, key_count),
            WEAVE_NO_ERROR);
  EXPECT_EQ(key_count, 0);
}

TEST_F(GroupKeyStoreTest, EnumerateKeysWithoutSpace) {
  const WeaveGroupKey test_key = CreateGroupKey(kTestKeyId, 0, kWeaveAppGroupKeySize);
  EXPECT_EQ(group_key_store_.StoreGroupKey(test_key), WEAVE_NO_ERROR);

  uint32_t key_ids[kMaxGroupKeys];
  uint8_t key_count;
  EXPECT_EQ(group_key_store_.EnumerateGroupKeys(WeaveKeyId::kNone, key_ids, 0, key_count),
            WEAVE_ERROR_BUFFER_TOO_SMALL);
  EXPECT_EQ(group_key_store_.EnumerateGroupKeys(WeaveKeyId::kNone, key_ids, 1, key_count),
            WEAVE_NO_ERROR);
}

TEST_F(GroupKeyStoreTest, DeleteKeysOfType) {
  const uint32_t none_key_id = WeaveKeyId::kType_None + 1;
  const uint32_t general1_key_id = WeaveKeyId::kType_General + 1;
  const uint32_t general2_key_id = WeaveKeyId::kType_General + 2;
  const WeaveGroupKey none_key = CreateGroupKey(none_key_id);
  const WeaveGroupKey general1_key = CreateGroupKey(general1_key_id);
  const WeaveGroupKey general2_key = CreateGroupKey(general2_key_id);

  EXPECT_EQ(group_key_store_.StoreGroupKey(none_key), WEAVE_NO_ERROR);
  EXPECT_EQ(group_key_store_.StoreGroupKey(general1_key), WEAVE_NO_ERROR);
  EXPECT_EQ(group_key_store_.StoreGroupKey(general2_key), WEAVE_NO_ERROR);

  uint32_t key_ids[kMaxGroupKeys];
  uint8_t key_count;
  EXPECT_EQ(group_key_store_.DeleteGroupKeysOfAType(WeaveKeyId::kType_General), WEAVE_NO_ERROR);
  EXPECT_EQ(
      group_key_store_.EnumerateGroupKeys(WeaveKeyId::kNone, key_ids, kMaxGroupKeys, key_count),
      WEAVE_NO_ERROR);
  EXPECT_EQ(key_count, 1);
  EXPECT_EQ(key_ids[0], none_key_id);
}

TEST_F(GroupKeyStoreTest, ClearKeys) {
  const uint32_t none_key_id = WeaveKeyId::kType_None + 1;
  const uint32_t general_key_id = WeaveKeyId::kType_General + 1;
  const WeaveGroupKey none_key = CreateGroupKey(none_key_id);
  const WeaveGroupKey general_key = CreateGroupKey(general_key_id);

  EXPECT_EQ(group_key_store_.StoreGroupKey(none_key), WEAVE_NO_ERROR);
  EXPECT_EQ(group_key_store_.StoreGroupKey(general_key), WEAVE_NO_ERROR);

  uint32_t key_ids[kMaxGroupKeys];
  uint8_t key_count;
  EXPECT_EQ(group_key_store_.Clear(), WEAVE_NO_ERROR);
  EXPECT_EQ(
      group_key_store_.EnumerateGroupKeys(WeaveKeyId::kNone, key_ids, kMaxGroupKeys, key_count),
      WEAVE_NO_ERROR);
  EXPECT_EQ(key_count, 0);
  EXPECT_EQ(ReadConfigStore().find("gk-"), std::string::npos);
}

TEST_F(GroupKeyStoreTest, RetrieveLastUsedEpochKeyId) {
  // Verify that retrieval succeeds even when the config value isn't stored.
  EXPECT_EQ(FuchsiaConfig::FactoryResetConfig(), WEAVE_NO_ERROR);
  EXPECT_EQ(group_key_store_.RetrieveLastUsedEpochKeyId(), WEAVE_NO_ERROR);

  // Verify that the key id is readable if already stored.
  EXPECT_EQ(FuchsiaConfig::FactoryResetConfig(), WEAVE_NO_ERROR);
  EXPECT_EQ(FuchsiaConfig::WriteConfigValue(FuchsiaConfig::kConfigKey_LastUsedEpochKeyId, 100u),
            WEAVE_NO_ERROR);
  EXPECT_EQ(group_key_store_.RetrieveLastUsedEpochKeyId(), WEAVE_NO_ERROR);
}

TEST_F(GroupKeyStoreTest, StoreLastUsedEpochKeyId) {
  EXPECT_EQ(FuchsiaConfig::WriteConfigValue(FuchsiaConfig::kConfigKey_LastUsedEpochKeyId, 100u),
            WEAVE_NO_ERROR);
  EXPECT_EQ(group_key_store_.RetrieveLastUsedEpochKeyId(), WEAVE_NO_ERROR);
  EXPECT_EQ(FuchsiaConfig::FactoryResetConfig(), WEAVE_NO_ERROR);
  EXPECT_EQ(group_key_store_.StoreLastUsedEpochKeyId(), WEAVE_NO_ERROR);
  EXPECT_EQ(ReadConfigStore(), "{\"last-used-epoch-key-id\":100}");
}

}  // namespace testing
}  // namespace adaptation
