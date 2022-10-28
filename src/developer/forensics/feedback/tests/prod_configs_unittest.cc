// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "src/developer/forensics/feedback/config.h"
#include "src/lib/files/path.h"

namespace forensics::feedback {
namespace {

class ProdConfigTest : public testing::Test {
 public:
  static std::optional<BuildTypeConfig> GetConfig(const std::string& config_filename) {
    return GetBuildTypeConfig(files::JoinPath("/pkg/data/configs", config_filename));
  }
};

TEST_F(ProdConfigTest, Default) {
  const std::optional<BuildTypeConfig> config = GetConfig("default.json");
  ASSERT_TRUE(config.has_value());

  EXPECT_FALSE(config->enable_data_redaction);
  EXPECT_FALSE(config->enable_hourly_snapshots);
  EXPECT_FALSE(config->enable_limit_inspect_data);
}

TEST_F(ProdConfigTest, User) {
  const std::optional<BuildTypeConfig> config = GetConfig("user.json");
  ASSERT_TRUE(config.has_value());

  EXPECT_TRUE(config->enable_data_redaction);
  EXPECT_FALSE(config->enable_hourly_snapshots);
  EXPECT_TRUE(config->enable_limit_inspect_data);
}

TEST_F(ProdConfigTest, Userdebug) {
  const std::optional<BuildTypeConfig> config = GetConfig("userdebug.json");
  ASSERT_TRUE(config.has_value());

  EXPECT_FALSE(config->enable_data_redaction);
  EXPECT_TRUE(config->enable_hourly_snapshots);
  EXPECT_FALSE(config->enable_limit_inspect_data);
}

}  // namespace
}  // namespace forensics::feedback
