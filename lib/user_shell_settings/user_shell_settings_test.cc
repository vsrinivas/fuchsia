// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/user_shell_settings/user_shell_settings.h"

#include <cmath>

#include "gtest/gtest.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace modular {

namespace internal {

// Expose some functions in modular::internal for testing.

template <typename T>
T GetObjectValue(const rapidjson::Value& object, const std::string& field_name);

extern template std::string GetObjectValue<std::string>(
    const rapidjson::Value& object, const std::string& field_name);

extern template float GetObjectValue<float>(const rapidjson::Value& object,
                                            const std::string& field_name);

extern template fuchsia::ui::policy::DisplayUsage
GetObjectValue<fuchsia::ui::policy::DisplayUsage>(
    const rapidjson::Value& object, const std::string& field_name);

std::vector<UserShellSettings> ParseUserShellSettings(const std::string& json);

}  // namespace internal

namespace {

using modular::internal::GetObjectValue;

TEST(UserShellSettingsTest,
     GetObjectValue_String_ReturnsEmptyStringOnNonStringType) {
  rapidjson::Document doc;
  std::string s;

  doc.Parse("5");
  s = GetObjectValue<std::string>(doc, "foo");
  EXPECT_EQ(s, "");

  doc.Parse("[]");
  s = GetObjectValue<std::string>(doc, "foo");
  EXPECT_EQ(s, "");

  doc.Parse("\"hi\"");
  s = GetObjectValue<std::string>(doc, "foo");
  EXPECT_EQ(s, "");
}

TEST(UserShellSettingsTest, GetObjectValue_String) {
  rapidjson::Document doc;
  doc.Parse("{ \"foo\": \"bar\" }");

  const std::string& s = GetObjectValue<std::string>(doc, "foo");
  EXPECT_EQ(s, "bar");
}

TEST(UserShellSettingsTest, GetObjectValue_Float_FailedParse) {
  rapidjson::Document doc;
  doc.Parse("{ \"foo\": \"bar\" }");

  const float f = GetObjectValue<float>(doc, "foo");
  EXPECT_TRUE(std::isnan(f));
}

TEST(UserShellSettingsTest, GetObjectValue_Float_SuccessfulParse) {
  rapidjson::Document doc;
  doc.Parse("{ \"foo\": \"3.141\" }");

  const float f = GetObjectValue<float>(doc, "foo");
  EXPECT_FLOAT_EQ(f, 3.141f);
}

TEST(UserShellSettingsTest, GetObjectValue_DisplayUsage_FailedParse) {
  using DisplayUsage = fuchsia::ui::policy::DisplayUsage;

  rapidjson::Document doc;
  doc.Parse("{ \"foo\": \"bar\" }");

  const DisplayUsage e = GetObjectValue<DisplayUsage>(doc, "foo");
  EXPECT_EQ(e, DisplayUsage::kUnknown);
}

TEST(UserShellSettingsTest, GetObjectValue_DisplayUsage_SuccessfulParse) {
  using DisplayUsage = fuchsia::ui::policy::DisplayUsage;

  rapidjson::Document doc;
  doc.Parse("{ \"foo\": \"midrange\" }");

  const DisplayUsage e = GetObjectValue<DisplayUsage>(doc, "foo");
  EXPECT_EQ(e, DisplayUsage::kMidrange);
}

TEST(UserShellSettingsTest, ParseUserShellSettings_ParseError) {
  EXPECT_EQ(internal::ParseUserShellSettings("a"),
            std::vector<UserShellSettings>());
  EXPECT_EQ(internal::ParseUserShellSettings("{}"),
            std::vector<UserShellSettings>());
}

TEST(UserShellSettingsTest, ParseUserShellSettings_ParseEmptyList) {
  EXPECT_EQ(internal::ParseUserShellSettings("[]"),
            std::vector<UserShellSettings>());
}

TEST(UserShellSettingsTest, ParseUserShellSettings_ParseNameOnly) {
  using DisplayUsage = fuchsia::ui::policy::DisplayUsage;

  const auto& settings =
      internal::ParseUserShellSettings(R"( [{ "name": "example_name" }] )")
          .at(0);

  EXPECT_EQ(settings.name, "example_name");
  EXPECT_TRUE(std::isnan(settings.screen_width));
  EXPECT_TRUE(std::isnan(settings.screen_height));
  EXPECT_EQ(settings.display_usage, DisplayUsage::kUnknown);
}

TEST(UserShellSettingsTest, ParseUserShellSettings_ParseCompleteEntry) {
  using DisplayUsage = fuchsia::ui::policy::DisplayUsage;

  const auto& settings = internal::ParseUserShellSettings(
                             R"( [{ "name": "example_name",
                                    "screen_width": "3.14",
                                    "screen_height": "2.718",
                                    "display_usage": "close" }] )")
                             .at(0);

  EXPECT_EQ(settings.name, "example_name");
  EXPECT_FLOAT_EQ(settings.screen_width, 3.14f);
  EXPECT_FLOAT_EQ(settings.screen_height, 2.718f);
  EXPECT_EQ(settings.display_usage, DisplayUsage::kClose);
}

TEST(UserShellSettingsTest, ParseUserShellSettings_ParseThreeEntries) {
  const auto& vector = internal::ParseUserShellSettings(
      R"( [{ "name": "example_name1" },
           { "name": "example_name2" },
           { "name": "example_name3" }] )");

  EXPECT_EQ(vector.size(), 3u);
}

}  // namespace

}  // namespace modular
