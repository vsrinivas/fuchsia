// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/metric_properties/metric_properties.h"

#include <cstdlib>
#include <filesystem>

#include <gtest/gtest.h>

#include "src/lib/files/scoped_temp_dir.h"

namespace analytics::metric_properties {

// To avoid polluting user's home directory, this test fixture will set $HOME to a temp directory
// during SetUp() and restore it during TearDown()
class FuchsiaPropertiesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    home_dir_ = std::getenv("HOME");
    ASSERT_EQ(setenv("HOME", temp_dir_.path().c_str(), 1), 0);
  }

  void TearDown() override { ASSERT_EQ(setenv("HOME", home_dir_.c_str(), 1), 0); }

 private:
  files::ScopedTempDir temp_dir_;
  std::filesystem::path home_dir_;
};

TEST_F(FuchsiaPropertiesTest, NonExistentProperty) {
  EXPECT_FALSE(Exists("not-created"));
  EXPECT_FALSE(Get("not-created").has_value());
  EXPECT_FALSE(GetBool("not-created").has_value());

  Delete("not-created");  // should be no-op
}

TEST_F(FuchsiaPropertiesTest, SetGetDelete) {
  Set("property", "value");
  EXPECT_TRUE(Exists("property"));
  EXPECT_EQ(*Get("property"), "value");

  Set("property", "new");
  EXPECT_EQ(*Get("property"), "new");

  Delete("property");
  EXPECT_FALSE(Exists("property"));
  EXPECT_FALSE(Get("property").has_value());
}

TEST_F(FuchsiaPropertiesTest, BooleanProperty) {
  SetBool("true", true);
  EXPECT_TRUE(*GetBool("true"));

  SetBool("false", false);
  EXPECT_FALSE(*GetBool("false"));

  Delete("true");
  Delete("false");
}

}  // namespace analytics::metric_properties
