// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/metric_properties/metric_properties.h"

#include <cstdlib>
#include <filesystem>
#include <optional>

#include <gtest/gtest.h>

#include "src/lib/analytics/cpp/metric_properties/optional_path.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace analytics::metric_properties {

// To avoid polluting user's home directory, this test fixture will set $HOME and $XDG_DATA_HOME to
// temp directories during SetUp() and restore it during TearDown()
class MetricPropertiesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // backup environment variables
    home_dir_ = internal::GetOptionalPathFromEnv("HOME");
    ASSERT_TRUE(home_dir_.has_value());
    xdg_dir_ = internal::GetOptionalPathFromEnv("XDG_DATA_HOME");

    ASSERT_EQ(setenv("HOME", temp_dir_.path().c_str(), 1), 0);
    ASSERT_EQ(unsetenv("XDG_DATA_HOME"), 0);
  }

  void TearDown() override {
    ASSERT_EQ(setenv("HOME", home_dir_->c_str(), 1), 0);
    if (xdg_dir_.has_value()) {
      ASSERT_EQ(setenv("XDG_DATA_HOME", xdg_dir_->c_str(), 1), 0);
    }
  }

  const std::string& GetTempPath() { return temp_dir_.path(); }

 private:
  files::ScopedTempDir temp_dir_;
  std::optional<std::filesystem::path> home_dir_;
  std::optional<std::filesystem::path> xdg_dir_;
};

TEST_F(MetricPropertiesTest, NonExistentProperty) {
  EXPECT_FALSE(Exists("not-created"));
  EXPECT_FALSE(Get("not-created").has_value());
  EXPECT_FALSE(GetBool("not-created").has_value());

  Delete("not-created");  // should be no-op
}

TEST_F(MetricPropertiesTest, SetGetDelete) {
  Set("property", "value");
  EXPECT_TRUE(Exists("property"));
  EXPECT_EQ(*Get("property"), "value");

  Set("property", "new");
  EXPECT_EQ(*Get("property"), "new");

  Delete("property");
  EXPECT_FALSE(Exists("property"));
  EXPECT_FALSE(Get("property").has_value());
}

TEST_F(MetricPropertiesTest, BooleanProperty) {
  SetBool("true", true);
  EXPECT_TRUE(*GetBool("true"));

  SetBool("false", false);
  EXPECT_FALSE(*GetBool("false"));

  Delete("true");
  Delete("false");
}

class MigrateMetricDirectoryTest : public MetricPropertiesTest {
 protected:
  void SetUp() override {
    MetricPropertiesTest::SetUp();
    std::filesystem::path home(GetTempPath());
    old_metric_dir_ = home / ".fuchsia" / "metrics";
#if defined(__APPLE__)
    new_metric_dir_ = home / "Library" / "Application Support" / "Fuchsia" / "metrics";
#else
    new_metric_dir_ = home / ".local" / "share" / "Fuchsia" / "metrics";
#endif
    ASSERT_FALSE(OldMetricDirExists());
    ASSERT_FALSE(NewMetricDirExists());
  }

  bool OldMetricDirExists() { return std::filesystem::exists(old_metric_dir_, ignore_); }

  bool NewMetricDirExists() { return std::filesystem::exists(new_metric_dir_, ignore_); }

  bool OldMetricDirIsSymlink() { return std::filesystem::is_symlink(old_metric_dir_, ignore_); }

  void CreateOldMetricDir() { ASSERT_TRUE(files::CreateDirectory(old_metric_dir_)); }

  void CreateNewMetricDir() { ASSERT_TRUE(files::CreateDirectory(new_metric_dir_)); }

 private:
  std::filesystem::path old_metric_dir_;
  std::filesystem::path new_metric_dir_;
  std::error_code ignore_;
};

TEST_F(MigrateMetricDirectoryTest, HasNoMetricDirectory) {
  MigrateMetricDirectory();
  EXPECT_FALSE(NewMetricDirExists());
  EXPECT_FALSE(OldMetricDirExists());

  Set("property", "value");
  EXPECT_TRUE(NewMetricDirExists());
  EXPECT_FALSE(OldMetricDirExists());
}

TEST_F(MigrateMetricDirectoryTest, HasOldMetricDirectory) {
  CreateOldMetricDir();
  MigrateMetricDirectory();
  EXPECT_TRUE(NewMetricDirExists());
  EXPECT_TRUE(OldMetricDirIsSymlink());
}

TEST_F(MigrateMetricDirectoryTest, HasNewMetricDirectory) {
  CreateNewMetricDir();
  MigrateMetricDirectory();
  EXPECT_TRUE(NewMetricDirExists());
  EXPECT_FALSE(OldMetricDirExists());
}

TEST_F(MigrateMetricDirectoryTest, HasBothMetricDirectory) {
  CreateOldMetricDir();
  CreateNewMetricDir();
  MigrateMetricDirectory();
  EXPECT_TRUE(NewMetricDirExists());
  EXPECT_TRUE(OldMetricDirExists());
  EXPECT_FALSE(OldMetricDirIsSymlink());
}

}  // namespace analytics::metric_properties
