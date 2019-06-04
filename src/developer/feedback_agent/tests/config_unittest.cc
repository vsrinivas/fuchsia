// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback_agent/config.h"

#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>

#include "src/lib/fxl/test/test_settings.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace fuchsia {
namespace feedback {
namespace {

void CheckEmptyConfig(const Config& config) {
  EXPECT_TRUE(config.annotation_allowlist.empty());
  EXPECT_TRUE(config.attachment_allowlist.empty());
}

TEST(ConfigTest, ParseConfig_ValidConfig) {
  Config config;
  ASSERT_EQ(ParseConfig("/pkg/data/valid_config.json", &config), ZX_OK);
  EXPECT_THAT(config.annotation_allowlist, testing::UnorderedElementsAreArray({
                                               "foo",
                                           }));
  EXPECT_THAT(config.attachment_allowlist, testing::UnorderedElementsAreArray({
                                               "log.kernel",
                                               "log.syslog",
                                           }));
}

TEST(ConfigTest, ParseConfig_ValidConfigEmptyList) {
  Config config;
  ASSERT_EQ(ParseConfig("/pkg/data/valid_config_empty_list.json", &config),
            ZX_OK);
  EXPECT_THAT(config.annotation_allowlist, testing::UnorderedElementsAreArray({
                                               "foo",
                                           }));
  EXPECT_TRUE(config.attachment_allowlist.empty());
}

TEST(ConfigTest, ParseConfig_MissingConfig) {
  Config config;
  ASSERT_EQ(ParseConfig("undefined file", &config), ZX_ERR_IO);
  CheckEmptyConfig(config);
}

TEST(ConfigTest, ParseConfig_BadConfig_DuplicatedAttachmentKey) {
  Config config;
  ASSERT_EQ(
      ParseConfig("/pkg/data/bad_schema_duplicated_attachment_key_config.json",
                  &config),
      ZX_ERR_INTERNAL);
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

}  // namespace
}  // namespace feedback
}  // namespace fuchsia

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"feedback_agent", "test"});
  return RUN_ALL_TESTS();
}
