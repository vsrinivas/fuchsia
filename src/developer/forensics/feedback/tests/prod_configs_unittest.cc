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
  static std::optional<BoardConfig> GetConfig(const std::string& config_filename) {
    return GetBoardConfig(files::JoinPath("/pkg/data/configs", config_filename));
  }
};

TEST_F(ProdConfigTest, DefaultBoard) {
  const std::optional<BoardConfig> config = GetConfig("default.json");
  ASSERT_TRUE(config.has_value());

  EXPECT_EQ(config->persisted_logs_num_files, 8u);
  EXPECT_EQ(config->persisted_logs_total_size, StorageSize::Kilobytes(512));
}

}  // namespace
}  // namespace forensics::feedback
