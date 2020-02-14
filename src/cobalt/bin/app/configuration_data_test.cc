// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/configuration_data.h"

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "third_party/abseil-cpp/absl/strings/match.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt::test {

using testing::ContainsRegex;

const char kTestDir[] = "/tmp/cobalt_config_test";

bool WriteFile(const std::string& file, const std::string& to_write) {
  return files::WriteFile(std::string(kTestDir) + std::string("/") + file, to_write.c_str(),
                          to_write.length());
}

// Tests behavior when there are no config files.
TEST(ConfigTest, Empty) {
  EXPECT_TRUE(files::DeletePath(kTestDir, true));
  EXPECT_TRUE(files::CreateDirectory(kTestDir));

  FuchsiaConfigurationData config_data(kTestDir, kTestDir);

  EXPECT_TRUE(absl::StrContains(config_data.AnalyzerPublicKeyPath(), "prod"));
  auto env = config_data.GetBackendEnvironment();
  EXPECT_EQ(config::Environment::PROD, env);
  EXPECT_TRUE(absl::StrContains(config_data.ShufflerPublicKeyPath(), "prod"));
  EXPECT_EQ(cobalt::ReleaseStage::GA, config_data.GetReleaseStage());
}

// Tests behavior when there is one valid config file.
TEST(ConfigTest, OneValidFile) {
  EXPECT_TRUE(files::DeletePath(kTestDir, true));
  EXPECT_TRUE(files::CreateDirectory(kTestDir));
  EXPECT_TRUE(WriteFile("cobalt_environment", "PROD"));

  FuchsiaConfigurationData config_data(kTestDir, kTestDir);

  EXPECT_TRUE(absl::StrContains(config_data.AnalyzerPublicKeyPath(), "prod"));
  auto env = config_data.GetBackendEnvironment();
  EXPECT_EQ(config::Environment::PROD, env);
  EXPECT_TRUE(absl::StrContains(config_data.ShufflerPublicKeyPath(), "prod"));
  EXPECT_EQ(cobalt::ReleaseStage::GA, config_data.GetReleaseStage());
}

// Tests behavior when there is one invalid config file.
TEST(ConfigTest, OneInvalidFile) {
  EXPECT_TRUE(files::DeletePath(kTestDir, true));
  EXPECT_TRUE(files::CreateDirectory(kTestDir));
  EXPECT_TRUE(WriteFile("cobalt_environment", "INVALID"));

  FuchsiaConfigurationData config_data(kTestDir, kTestDir);

  EXPECT_TRUE(absl::StrContains(config_data.AnalyzerPublicKeyPath(), "prod"));
  auto env = config_data.GetBackendEnvironment();
  EXPECT_EQ(config::Environment::PROD, env);
  EXPECT_TRUE(absl::StrContains(config_data.ShufflerPublicKeyPath(), "prod"));
  EXPECT_EQ(cobalt::ReleaseStage::GA, config_data.GetReleaseStage());
}

TEST(ConfigTest, ReleaseStageGA) {
  EXPECT_TRUE(files::DeletePath(kTestDir, true));
  EXPECT_TRUE(files::CreateDirectory(kTestDir));
  EXPECT_TRUE(WriteFile("config.json", "{\"release_stage\": \"GA\"}"));

  FuchsiaConfigurationData config_data(kTestDir, kTestDir);

  EXPECT_EQ(cobalt::ReleaseStage::GA, config_data.GetReleaseStage());
}

TEST(ConfigTest, ReleaseStageDEBUG) {
  EXPECT_TRUE(files::DeletePath(kTestDir, true));
  EXPECT_TRUE(files::CreateDirectory(kTestDir));
  EXPECT_TRUE(WriteFile("config.json", "{\"release_stage\": \"DEBUG\"}"));

  FuchsiaConfigurationData config_data(kTestDir, kTestDir);

  EXPECT_EQ(cobalt::ReleaseStage::DEBUG, config_data.GetReleaseStage());
}

TEST(ConfigTest, DataCollectionPolicyDoNotUpload) {
  EXPECT_TRUE(files::DeletePath(kTestDir, true));
  EXPECT_TRUE(files::CreateDirectory(kTestDir));
  EXPECT_TRUE(WriteFile("config.json", "{\"default_data_collection_policy\": \"DO_NOT_UPLOAD\"}"));

  FuchsiaConfigurationData config_data(kTestDir, kTestDir);

  EXPECT_EQ(cobalt::CobaltService::DataCollectionPolicy::DO_NOT_UPLOAD,
            config_data.GetDataCollectionPolicy());
}

TEST(ConfigTest, DataCollectionPolicyCollectAndUpload) {
  EXPECT_TRUE(files::DeletePath(kTestDir, true));
  EXPECT_TRUE(files::CreateDirectory(kTestDir));
  EXPECT_TRUE(
      WriteFile("config.json", "{\"default_data_collection_policy\": \"COLLECT_AND_UPLOAD\"}"));

  FuchsiaConfigurationData config_data(kTestDir, kTestDir);

  EXPECT_EQ(cobalt::CobaltService::DataCollectionPolicy::COLLECT_AND_UPLOAD,
            config_data.GetDataCollectionPolicy());
}

TEST(ConfigTest, WatchForUserConsentDefault) {
  EXPECT_TRUE(files::DeletePath(kTestDir, true));
  EXPECT_TRUE(files::CreateDirectory(kTestDir));
  EXPECT_TRUE(WriteFile("config.json", "{}"));

  FuchsiaConfigurationData config_data(kTestDir, kTestDir);

  EXPECT_EQ(true, config_data.GetWatchForUserConsent());
}

TEST(ConfigTest, WatchForUserConsentTrue) {
  EXPECT_TRUE(files::DeletePath(kTestDir, true));
  EXPECT_TRUE(files::CreateDirectory(kTestDir));
  EXPECT_TRUE(WriteFile("config.json", "{\"watch_for_user_consent\":true}"));

  FuchsiaConfigurationData config_data(kTestDir, kTestDir);

  EXPECT_EQ(true, config_data.GetWatchForUserConsent());
}

TEST(ConfigTest, WatchForUserConsentFalse) {
  EXPECT_TRUE(files::DeletePath(kTestDir, true));
  EXPECT_TRUE(files::CreateDirectory(kTestDir));
  EXPECT_TRUE(WriteFile("config.json", "{\"watch_for_user_consent\":false}"));

  FuchsiaConfigurationData config_data(kTestDir, kTestDir);

  EXPECT_EQ(false, config_data.GetWatchForUserConsent());
}

TEST(ConfigTest, GetApiKeyNotEmpty) {
  EXPECT_TRUE(files::DeletePath(kTestDir, true));
  EXPECT_TRUE(files::CreateDirectory(kTestDir));

  FuchsiaConfigurationData config_data(kTestDir, kTestDir);
  auto api_key = config_data.GetApiKey();
  EXPECT_EQ(api_key, "cobalt-default-api-key");
}

TEST(ConfigTest, GetApiKey) {
  EXPECT_TRUE(files::DeletePath(kTestDir, true));
  EXPECT_TRUE(files::CreateDirectory(kTestDir));
  EXPECT_TRUE(WriteFile("api_key.hex", "deadbeef"));

  FuchsiaConfigurationData config_data(kTestDir, kTestDir);
  auto api_key = config_data.GetApiKey();
  EXPECT_EQ(api_key, "\xDE\xAD\xBE\xEF");
}

TEST(JSONHelper, FailsToReadInvalidConfig) {
  EXPECT_TRUE(files::DeletePath(kTestDir, true));
  EXPECT_TRUE(files::CreateDirectory(kTestDir));
  EXPECT_TRUE(WriteFile("config.json", "{"));

  JSONHelper helper(files::JoinPath(kTestDir, "config.json"));
  EXPECT_FALSE(helper.GetString("invalid_config").ok());
  EXPECT_THAT(helper.GetString("invalid_config").status().error_message(),
              ContainsRegex("Failed to parse"));

  EXPECT_FALSE(helper.GetBool("invalid_config").ok());
  EXPECT_THAT(helper.GetBool("invalid_config").status().error_message(),
              ContainsRegex("Failed to parse"));
}

TEST(JSONHelper, FailsToReadAbsentKeys) {
  EXPECT_TRUE(files::DeletePath(kTestDir, true));
  EXPECT_TRUE(files::CreateDirectory(kTestDir));
  EXPECT_TRUE(WriteFile("config.json", "{}"));

  JSONHelper helper(files::JoinPath(kTestDir, "config.json"));
  EXPECT_FALSE(helper.GetString("not_present").ok());
  EXPECT_THAT(helper.GetString("not_present").status().error_message(),
              ContainsRegex("not present"));

  EXPECT_FALSE(helper.GetBool("not_present").ok());
  EXPECT_THAT(helper.GetBool("not_present").status().error_message(), ContainsRegex("not present"));
}

TEST(JSONHelper, FailsToReadWrongType) {
  EXPECT_TRUE(files::DeletePath(kTestDir, true));
  EXPECT_TRUE(files::CreateDirectory(kTestDir));
  EXPECT_TRUE(WriteFile("config.json", "{\"not_string\":null,\"not_bool\":\"test\"}"));

  JSONHelper helper(files::JoinPath(kTestDir, "config.json"));

  EXPECT_FALSE(helper.GetString("not_string").ok());
  EXPECT_THAT(helper.GetString("not_string").status().error_message(),
              ContainsRegex("is not of type string"));
  EXPECT_THAT(helper.GetString("not_string").status().error_details(),
              ContainsRegex("is expected to be a string"));

  EXPECT_FALSE(helper.GetBool("not_bool").ok());
  EXPECT_THAT(helper.GetBool("not_bool").status().error_message(),
              ContainsRegex("is not of type bool"));
  EXPECT_THAT(helper.GetBool("not_bool").status().error_details(),
              ContainsRegex("is expected to be a bool"));
}

TEST(JSONHelper, CanRead) {
  EXPECT_TRUE(files::DeletePath(kTestDir, true));
  EXPECT_TRUE(files::CreateDirectory(kTestDir));
  EXPECT_TRUE(WriteFile("config.json", "{\"a_string\":\"a value\",\"a_bool\":true}"));

  JSONHelper helper(files::JoinPath(kTestDir, "config.json"));

  EXPECT_TRUE(helper.GetString("a_string").ok());
  EXPECT_EQ(helper.GetString("a_string").ConsumeValueOrDie(), "a value");

  EXPECT_TRUE(helper.GetBool("a_bool").ok());
  EXPECT_EQ(helper.GetBool("a_bool").ConsumeValueOrDie(), true);
}

}  // namespace cobalt::test
