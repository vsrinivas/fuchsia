// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/configuration_data.h"

#include "gtest/gtest.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "third_party/abseil-cpp/absl/strings/match.h"

namespace cobalt::test {

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

  EXPECT_TRUE(absl::StrContains(config_data.AnalyzerPublicKeyPath(), "devel"));
  auto envs = config_data.GetBackendEnvironments();
  ASSERT_EQ(1, envs.size());
  EXPECT_EQ(config::Environment::DEVEL, envs[0]);
  EXPECT_TRUE(absl::StrContains(config_data.ShufflerPublicKeyPath(envs[0]), "devel"));
  EXPECT_EQ(cobalt::ReleaseStage::GA, config_data.GetReleaseStage());
}

// Tests behavior when there is one valid config file.
TEST(ConfigTest, OneValidFile) {
  EXPECT_TRUE(files::DeletePath(kTestDir, true));
  EXPECT_TRUE(files::CreateDirectory(kTestDir));
  EXPECT_TRUE(WriteFile("cobalt_environment", "PROD"));

  FuchsiaConfigurationData config_data(kTestDir, kTestDir);

  EXPECT_TRUE(absl::StrContains(config_data.AnalyzerPublicKeyPath(), "prod"));
  auto envs = config_data.GetBackendEnvironments();
  ASSERT_EQ(1, envs.size());
  EXPECT_EQ(config::Environment::PROD, envs[0]);
  EXPECT_TRUE(absl::StrContains(config_data.ShufflerPublicKeyPath(envs[0]), "prod"));
  EXPECT_EQ(cobalt::ReleaseStage::GA, config_data.GetReleaseStage());
}

// Tests behavior when there is one invalid config file.
TEST(ConfigTest, OneInvalidFile) {
  EXPECT_TRUE(files::DeletePath(kTestDir, true));
  EXPECT_TRUE(files::CreateDirectory(kTestDir));
  EXPECT_TRUE(WriteFile("cobalt_environment", "INVALID"));

  FuchsiaConfigurationData config_data(kTestDir, kTestDir);

  EXPECT_TRUE(absl::StrContains(config_data.AnalyzerPublicKeyPath(), "devel"));
  auto envs = config_data.GetBackendEnvironments();
  ASSERT_EQ(1, envs.size());
  EXPECT_EQ(config::Environment::DEVEL, envs[0]);
  EXPECT_TRUE(absl::StrContains(config_data.ShufflerPublicKeyPath(envs[0]), "devel"));
  EXPECT_EQ(cobalt::ReleaseStage::GA, config_data.GetReleaseStage());
}

// Tests behavior when there are multiple backends.
TEST(ConfigTest, MultipleBackends) {
  EXPECT_TRUE(files::DeletePath(kTestDir, true));
  EXPECT_TRUE(files::CreateDirectory(kTestDir));
  EXPECT_TRUE(WriteFile("cobalt_environment", "DEVEL_AND_PROD"));

  FuchsiaConfigurationData config_data(kTestDir, kTestDir);

  EXPECT_TRUE(absl::StrContains(config_data.AnalyzerPublicKeyPath(), "devel"));
  auto envs = config_data.GetBackendEnvironments();
  EXPECT_EQ(std::vector({config::Environment::PROD, config::Environment::DEVEL}), envs);
  ASSERT_EQ(2, envs.size());
  EXPECT_TRUE(absl::StrContains(config_data.ShufflerPublicKeyPath(envs[0]), "prod"));
  EXPECT_TRUE(absl::StrContains(config_data.ShufflerPublicKeyPath(envs[1]), "devel"));
  EXPECT_EQ(cobalt::ReleaseStage::GA, config_data.GetReleaseStage());
}

TEST(ConfigTest, ReleaseStagePathGA) {
  EXPECT_TRUE(files::DeletePath(kTestDir, true));
  EXPECT_TRUE(files::CreateDirectory(kTestDir));
  EXPECT_TRUE(WriteFile("release_stage", "GA"));

  FuchsiaConfigurationData config_data(kTestDir, kTestDir);

  EXPECT_EQ(cobalt::ReleaseStage::GA, config_data.GetReleaseStage());
}

TEST(ConfigTest, ReleaseStagePathDEBUG) {
  EXPECT_TRUE(files::DeletePath(kTestDir, true));
  EXPECT_TRUE(files::CreateDirectory(kTestDir));
  EXPECT_TRUE(WriteFile("release_stage", "DEBUG"));

  FuchsiaConfigurationData config_data(kTestDir, kTestDir);

  EXPECT_EQ(cobalt::ReleaseStage::DEBUG, config_data.GetReleaseStage());
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

}  // namespace cobalt::test
