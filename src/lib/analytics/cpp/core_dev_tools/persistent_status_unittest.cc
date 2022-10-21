// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/core_dev_tools/persistent_status.h"

#include <cstdlib>
#include <filesystem>

#include <gtest/gtest.h>

#include "src/lib/analytics/cpp/metric_properties/metric_properties.h"
#include "src/lib/analytics/cpp/metric_properties/optional_path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/uuid/uuid.h"

namespace analytics::core_dev_tools::internal {

// To avoid polluting user's home directory, this test fixture will set $HOME to a temp directory
// during SetUp() and restore it during TearDown()
class PersistentStatusTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // backup environment variables
    home_dir_ = metric_properties::internal::GetOptionalPathFromEnv("HOME");
    ASSERT_TRUE(home_dir_.has_value());
    xdg_dir_ = metric_properties::internal::GetOptionalPathFromEnv("XDG_DATA_HOME");

    ASSERT_EQ(setenv("HOME", temp_dir_.path().c_str(), 1), 0);
    ASSERT_EQ(unsetenv("XDG_DATA_HOME"), 0);
  }

  void TearDown() override {
    ASSERT_EQ(setenv("HOME", home_dir_->c_str(), 1), 0);
    if (xdg_dir_.has_value()) {
      ASSERT_EQ(setenv("XDG_DATA_HOME", xdg_dir_->c_str(), 1), 0);
    }
  }

 private:
  files::ScopedTempDir temp_dir_;
  std::optional<std::filesystem::path> home_dir_;
  std::optional<std::filesystem::path> xdg_dir_;
};

TEST_F(PersistentStatusTest, All) {
  EXPECT_TRUE(PersistentStatus::IsFirstLaunchOfFirstTool());

  PersistentStatus::Enable();
  EXPECT_TRUE(PersistentStatus::IsEnabled());
  EXPECT_FALSE(PersistentStatus::IsFirstLaunchOfFirstTool());
  EXPECT_TRUE(metric_properties::Exists("uuid"));
  EXPECT_TRUE(uuid::IsValid(PersistentStatus::GetUuid()));

  PersistentStatus::Disable();
  EXPECT_FALSE(PersistentStatus::IsEnabled());
  EXPECT_FALSE(PersistentStatus::IsFirstLaunchOfFirstTool());
  EXPECT_FALSE(metric_properties::Exists("uuid"));

  EXPECT_TRUE(PersistentStatus("tool").IsFirstDirectLaunch());
  PersistentStatus("tool").MarkAsDirectlyLaunched();
  EXPECT_FALSE(PersistentStatus("tool").IsFirstDirectLaunch());
}

}  // namespace analytics::core_dev_tools::internal
