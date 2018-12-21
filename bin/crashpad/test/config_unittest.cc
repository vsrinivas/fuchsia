// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/crashpad/config.h"

#include <gtest/gtest.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>

namespace fuchsia {
namespace crash {
namespace {

TEST(ConfigTest, ParseConfig_ValidConfig) {
  Config config;
  ASSERT_EQ(ParseConfig("/pkg/data/valid_config.json", &config), ZX_OK);
  EXPECT_EQ(config.local_crashpad_database_path, "/data/crashes");
  EXPECT_FALSE(config.enable_upload_to_crash_server);
}

TEST(ConfigTest, ParseConfig_MissingConfig) {
  Config config;
  ASSERT_EQ(ParseConfig("undefined file", &config), ZX_ERR_IO);
}

TEST(ConfigTest, ParseConfig_BadSchemaSpuriousFieldConfig) {
  Config config;
  ASSERT_EQ(
      ParseConfig("/pkg/data/bad_schema_spurious_field_config.json", &config),
      ZX_ERR_INTERNAL);
}

TEST(ConfigTest, ParseConfig_BadSchemaMissingRequiredConfig) {
  Config config;
  ASSERT_EQ(
      ParseConfig("/pkg/data/bad_schema_missing_required_field_config.json",
                  &config),
      ZX_ERR_INTERNAL);
}

}  // namespace
}  // namespace crash
}  // namespace fuchsia

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"crash", "test"});
  return RUN_ALL_TESTS();
}
