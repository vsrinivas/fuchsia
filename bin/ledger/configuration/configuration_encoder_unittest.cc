// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/configuration/configuration_encoder.h"

#include "apps/ledger/src/configuration/configuration.h"
#include "gtest/gtest.h"
#include "lib/ftl/files/scoped_temp_dir.h"

namespace configuration {
namespace {
class ConfigurationEncoderTest : public ::testing::Test {
 public:
  ConfigurationEncoderTest() {}
  ~ConfigurationEncoderTest() override {}

 protected:
  files::ScopedTempDir temp_dir_;
};

TEST_F(ConfigurationEncoderTest, EmptyData) {
  std::string file_path;
  temp_dir_.NewTempFile(&file_path);

  Configuration expected_config;
  EXPECT_TRUE(ConfigurationEncoder::Write(file_path, expected_config));

  Configuration actual_config;
  EXPECT_TRUE(ConfigurationEncoder::Decode(file_path, &actual_config));
  EXPECT_EQ(expected_config, actual_config);
}

TEST_F(ConfigurationEncoderTest, EncodeWithSync) {
  std::string file_path;
  temp_dir_.NewTempFile(&file_path);

  Configuration expected_config;
  expected_config.use_sync = true;
  expected_config.sync_params.firebase_id = "example";
  expected_config.sync_params.firebase_prefix = "/testing/";

  EXPECT_TRUE(ConfigurationEncoder::Write(file_path, expected_config));

  Configuration actual_config;
  EXPECT_TRUE(ConfigurationEncoder::Decode(file_path, &actual_config));
  EXPECT_EQ(expected_config, actual_config);
}
}  // namespace
}  // namespace configuration
