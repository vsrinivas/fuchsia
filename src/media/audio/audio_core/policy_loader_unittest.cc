// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/policy_loader.h"

#include <lib/gtest/test_loop_fixture.h>

#include "src/media/audio/audio_core/policy_loader_unittest_data.h"

namespace media::audio {
class AudioAdminUnitTest : public gtest::TestLoopFixture {};

static const char* const allowed_render_usages[] = {"BACKGROUND", "MEDIA", "INTERRUPTION",
                                                    "SYSTEM_AGENT", "COMMUNICATION"};
static_assert(GTEST_ARRAY_SIZE_(allowed_render_usages) == fuchsia::media::RENDER_USAGE_COUNT,
              "New Render Usage(s) added to fidl without updating tests");

TEST_F(AudioAdminUnitTest, InvalidRenderUsage) {
  const char bad_render_usage[] = "INVALID";
  {
    rapidjson::Value v(rapidjson::StringRef(bad_render_usage));
    auto render_usage = PolicyLoader::JsonToRenderUsage(v);
    EXPECT_FALSE(render_usage);
  }
}

TEST_F(AudioAdminUnitTest, ValidRenderUsages) {
  for (auto usage : allowed_render_usages) {
    rapidjson::Value v(rapidjson::StringRef(usage));
    auto render_usage = PolicyLoader::JsonToRenderUsage(v);
    EXPECT_TRUE(render_usage);
  }
}

static const char* const allowed_capture_usages[] = {"BACKGROUND", "FOREGROUND", "SYSTEM_AGENT",
                                                     "COMMUNICATION"};
static_assert(GTEST_ARRAY_SIZE_(allowed_capture_usages) == fuchsia::media::CAPTURE_USAGE_COUNT,
              "New Capture Usage(s) added to fidl without updating tests");
TEST_F(AudioAdminUnitTest, InvalidCaptureUsages) {
  const char bad_capture_usage[] = "INVALID";
  {
    rapidjson::Value v(rapidjson::StringRef(bad_capture_usage));
    auto capture_usage = PolicyLoader::JsonToCaptureUsage(v);
    EXPECT_FALSE(capture_usage);
  }
}

TEST_F(AudioAdminUnitTest, ValidCaptureUsages) {
  for (auto usage : allowed_capture_usages) {
    rapidjson::Value v(rapidjson::StringRef(usage));
    auto capture_usage = PolicyLoader::JsonToCaptureUsage(v);
    EXPECT_TRUE(capture_usage);
  }
}

static const char* const allowed_behaviors[] = {"NONE", "DUCK", "MUTE"};
TEST_F(AudioAdminUnitTest, Behaviors) {
  const char bad_behavior[] = "INVALID";
  {
    rapidjson::Value v(rapidjson::StringRef(bad_behavior));
    auto parsed_behavior = PolicyLoader::JsonToBehavior(v);
    EXPECT_FALSE(parsed_behavior);
  }

  for (auto behavior : allowed_behaviors) {
    rapidjson::Value v(rapidjson::StringRef(behavior));
    auto parsed_behavior = PolicyLoader::JsonToBehavior(v);
    EXPECT_TRUE(parsed_behavior);
  }
}

TEST_F(AudioAdminUnitTest, BadConfigs) {
  // Configs that aren't complete enough to use.
  EXPECT_FALSE(PolicyLoader::ParseConfig(test::no_rules));
  EXPECT_FALSE(PolicyLoader::ParseConfig(test::no_active));
  EXPECT_FALSE(PolicyLoader::ParseConfig(test::no_affected));
  EXPECT_FALSE(PolicyLoader::ParseConfig(test::no_behavior));

  // Malformed configs.
  EXPECT_FALSE(PolicyLoader::ParseConfig(test::rules_not_array));
  EXPECT_FALSE(PolicyLoader::ParseConfig(test::rules_array_not_rules));

  // Configs that have all the required parts, but have invalid values.
  EXPECT_FALSE(PolicyLoader::ParseConfig(test::invalid_renderusage));
  EXPECT_FALSE(PolicyLoader::ParseConfig(test::invalid_captureusage));
  EXPECT_FALSE(PolicyLoader::ParseConfig(test::invalid_behavior));
}

TEST_F(AudioAdminUnitTest, GoodConfigs) {
  // Explicitly passing no rules is an acceptable configuration.
  EXPECT_TRUE(PolicyLoader::ParseConfig(test::empty_rules_json));

  EXPECT_TRUE(PolicyLoader::ParseConfig(test::ignored_key));

  // Test each possible combination of render and capture usage.
  EXPECT_TRUE(PolicyLoader::ParseConfig(test::render_render));
  EXPECT_TRUE(PolicyLoader::ParseConfig(test::render_capture));
  EXPECT_TRUE(PolicyLoader::ParseConfig(test::capture_render));
  EXPECT_TRUE(PolicyLoader::ParseConfig(test::capture_capture));
}
}  // namespace media::audio
