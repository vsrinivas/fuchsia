// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/crashpad_agent/config.h"

#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>

#include "src/lib/fxl/test/test_settings.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace fuchsia {
namespace crash {
namespace {

void CheckEmptyConfig(const Config& config) {
  EXPECT_EQ(config.local_crashpad_database_path, "");
  EXPECT_FALSE(config.enable_upload_to_crash_server);
  EXPECT_EQ(config.crash_server_url, nullptr);
}

TEST(ConfigTest, ParseConfig_ValidConfig_NoUpload) {
  Config config;
  ASSERT_EQ(ParseConfig("/pkg/data/valid_config_no_upload.json", &config),
            ZX_OK);
  EXPECT_EQ(config.local_crashpad_database_path, "/data/crashes");
  EXPECT_FALSE(config.enable_upload_to_crash_server);
  EXPECT_EQ(config.crash_server_url, nullptr);
}

TEST(ConfigTest, ParseConfig_ValidConfig_Upload) {
  Config config;
  ASSERT_EQ(ParseConfig("/pkg/data/valid_config_upload.json", &config), ZX_OK);
  EXPECT_EQ(config.local_crashpad_database_path, "/data/crashes");
  EXPECT_TRUE(config.enable_upload_to_crash_server);
  EXPECT_EQ(*config.crash_server_url, "http://localhost:1234");
}

TEST(ConfigTest, ParseConfig_ValidConfig_NoUploadServerUrlIgnored) {
  Config config;
  ASSERT_EQ(ParseConfig("/pkg/data/valid_config_no_upload_spurious_server.json",
                        &config),
            ZX_OK);
  EXPECT_EQ(config.local_crashpad_database_path, "/data/crashes");
  EXPECT_FALSE(config.enable_upload_to_crash_server);
  // Even though a URL is set in the config file, we check that it is not set in
  // the struct.
  EXPECT_EQ(config.crash_server_url, nullptr);
}

TEST(ConfigTest, ParseConfig_MissingConfig) {
  Config config;
  ASSERT_EQ(ParseConfig("undefined file", &config), ZX_ERR_IO);
  CheckEmptyConfig(config);
}

TEST(ConfigTest, ParseConfig_BadConfig_SpuriousField) {
  Config config;
  ASSERT_EQ(
      ParseConfig("/pkg/data/bad_schema_spurious_field_config.json", &config),
      ZX_ERR_INTERNAL);
  CheckEmptyConfig(config);
}

TEST(ConfigTest, ParseConfig_BadConfig_MissingRequiredField) {
  Config config;
  ASSERT_EQ(
      ParseConfig("/pkg/data/bad_schema_missing_required_field_config.json",
                  &config),
      ZX_ERR_INTERNAL);
  CheckEmptyConfig(config);
}

TEST(ConfigTest, ParseConfig_BadConfig_MissingServerUrlWithUploadEnabled) {
  Config config;
  ASSERT_EQ(
      ParseConfig("/pkg/data/bad_schema_missing_server_config.json", &config),
      ZX_ERR_INTERNAL);
  CheckEmptyConfig(config);
}

}  // namespace
}  // namespace crash
}  // namespace fuchsia

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"crash", "test"});
  return RUN_ALL_TESTS();
}
