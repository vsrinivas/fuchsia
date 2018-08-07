// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/cmx/sandbox.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "garnet/lib/json/json_parser.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace component {
namespace {

class SandboxMetadataTest : public ::testing::Test {
 protected:
  void ExpectFailedParse(const std::string& json,
                         const std::string& expected_error) {
    std::string error;
    SandboxMetadata sandbox;
    EXPECT_FALSE(ParseFrom(&sandbox, json, &error));
    EXPECT_TRUE(sandbox.IsNull());
    EXPECT_EQ(error, expected_error);
  }

  bool ParseFrom(SandboxMetadata* sandbox, const std::string& json,
                 std::string* error) {
    json::JSONParser parser;
    rapidjson::Document document = parser.ParseFromString(json, "test_file");
    EXPECT_FALSE(parser.HasError());
    EXPECT_TRUE(sandbox->IsNull());
    const bool ret = sandbox->Parse(document, &parser);
    if (parser.HasError()) {
      *error = parser.error_str();
    }
    return ret;
  }
};

TEST_F(SandboxMetadataTest, Parse) {
  // empty
  {
    SandboxMetadata sandbox;
    std::string error;
    EXPECT_TRUE(ParseFrom(&sandbox, R"JSON({})JSON", &error));
    EXPECT_EQ(error, "");
    EXPECT_FALSE(sandbox.IsNull());
  }

  // dev
  {
    SandboxMetadata sandbox;
    std::string error;
    EXPECT_TRUE(
        ParseFrom(&sandbox, R"JSON({ "dev": [ "class/input" ] })JSON", &error));
    EXPECT_EQ(error, "");
    EXPECT_FALSE(sandbox.IsNull());
    EXPECT_THAT(sandbox.dev(), ::testing::ElementsAre("class/input"));
  }

  // system
  {
    SandboxMetadata sandbox;
    std::string error;
    EXPECT_TRUE(
        ParseFrom(&sandbox, R"JSON({ "system": [ "data" ] })JSON", &error));
    EXPECT_EQ(error, "");
    EXPECT_FALSE(sandbox.IsNull());
    EXPECT_THAT(sandbox.system(), ::testing::ElementsAre("data"));
    EXPECT_FALSE(sandbox.has_services());
  }

  // services
  {
    SandboxMetadata sandbox;
    std::string error;
    EXPECT_TRUE(ParseFrom(
        &sandbox, R"JSON({ "services": [ "fuchsia.sys.Launcher" ] })JSON",
        &error));
    EXPECT_EQ(error, "");
    EXPECT_FALSE(sandbox.IsNull());
    EXPECT_THAT(sandbox.services(),
                ::testing::ElementsAre("fuchsia.sys.Launcher"));
    EXPECT_TRUE(sandbox.has_services());
  }

  // pkgfs
  {
    SandboxMetadata sandbox;
    std::string error;
    EXPECT_TRUE(
        ParseFrom(&sandbox, R"JSON({ "pkgfs": [ "packages" ] })JSON", &error));
    EXPECT_EQ(error, "");
    EXPECT_FALSE(sandbox.IsNull());
    EXPECT_THAT(sandbox.pkgfs(), ::testing::ElementsAre("packages"));
  }

  // features
  {
    SandboxMetadata sandbox;
    std::string error;
    EXPECT_TRUE(ParseFrom(
        &sandbox, R"JSON({ "features": [ "vulkan", "shell" ] })JSON", &error));
    EXPECT_EQ(error, "");
    EXPECT_FALSE(sandbox.IsNull());
    EXPECT_THAT(sandbox.features(), ::testing::ElementsAre("vulkan", "shell"));
    EXPECT_TRUE(sandbox.HasFeature("vulkan"));
    EXPECT_TRUE(sandbox.HasFeature("shell"));
    EXPECT_FALSE(sandbox.HasFeature("banana"));
  }

  // boot
  {
    SandboxMetadata sandbox;
    std::string error;
    EXPECT_TRUE(
        ParseFrom(&sandbox, R"JSON({ "boot": [ "log" ] })JSON", &error));
    EXPECT_EQ(error, "");
    EXPECT_FALSE(sandbox.IsNull());
    EXPECT_THAT(sandbox.boot(), ::testing::ElementsAre("log"));
  }
}

TEST_F(SandboxMetadataTest, ParseWithErrors) {
  ExpectFailedParse(
      R"JSON({
        "dev": [ "class/input", 3 ],
        "services": 55
      })JSON",
      "test_file: Entry for 'dev' in sandbox is not a string.\n"
      "test_file: 'services' in sandbox is not an array.");
  ExpectFailedParse(
      R"JSON({
        "features": [ "vulkan", "deprecated-all-services" ],
        "services": [ "fuchsia.sys.Launcher" ]
      })JSON",
      "test_file: Sandbox may not include both 'services' and "
      "'deprecated-all-services'.");
}

}  // namespace
}  // namespace component
