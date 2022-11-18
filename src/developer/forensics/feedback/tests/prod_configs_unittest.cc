// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "src/developer/forensics/feedback/config.h"
#include "src/developer/forensics/utils/storage_size.h"
#include "src/lib/files/path.h"

namespace forensics::feedback {
namespace {

class ProdConfigTest : public testing::Test {
 public:
  static std::optional<BoardConfig> ReadBoardConfig(const std::string& config_filename) {
    return GetBoardConfig(files::JoinPath("/pkg/data/board/configs", config_filename));
  }

  static std::optional<BuildTypeConfig> ReadBuildTypeConfig(const std::string& config_filename) {
    return GetBuildTypeConfig(files::JoinPath("/pkg/data/build_type/configs", config_filename));
  }
};

TEST_F(ProdConfigTest, DefaultBoard) {
  const std::optional<BoardConfig> config = ReadBoardConfig("default.json");
  ASSERT_TRUE(config.has_value());

  EXPECT_EQ(config->persisted_logs_num_files, 8u);
  EXPECT_EQ(config->persisted_logs_total_size, StorageSize::Kilobytes(512));
  EXPECT_FALSE(config->snapshot_persistence_max_tmp_size.has_value());
  EXPECT_FALSE(config->snapshot_persistence_max_cache_size.has_value());
}

TEST_F(ProdConfigTest, Default) {
  const std::optional<BuildTypeConfig> config = ReadBuildTypeConfig("default.json");
  ASSERT_TRUE(config.has_value());

  EXPECT_FALSE(config->enable_data_redaction);
  EXPECT_FALSE(config->enable_hourly_snapshots);
  EXPECT_FALSE(config->enable_limit_inspect_data);
}

TEST_F(ProdConfigTest, User) {
  const std::optional<BuildTypeConfig> config = ReadBuildTypeConfig("user.json");
  ASSERT_TRUE(config.has_value());

  EXPECT_TRUE(config->enable_data_redaction);
  EXPECT_FALSE(config->enable_hourly_snapshots);
  EXPECT_TRUE(config->enable_limit_inspect_data);
}

TEST_F(ProdConfigTest, Userdebug) {
  const std::optional<BuildTypeConfig> config = ReadBuildTypeConfig("userdebug.json");
  ASSERT_TRUE(config.has_value());

  EXPECT_FALSE(config->enable_data_redaction);
  EXPECT_TRUE(config->enable_hourly_snapshots);
  EXPECT_FALSE(config->enable_limit_inspect_data);
}

}  // namespace
}  // namespace forensics::feedback
