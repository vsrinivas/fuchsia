// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/cmx/sandbox.h"

#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rapidjson/document.h"
#include "src/lib/json_parser/json_parser.h"

namespace component {
namespace {

class SandboxMetadataTest : public ::testing::Test {
 protected:
  void ExpectFailedParse(const std::string& json, const std::string& expected_error) {
    SandboxMetadata sandbox;
    std::optional parse_error = ParseFrom(&sandbox, json);
    EXPECT_TRUE(sandbox.IsNull());
    ASSERT_TRUE(parse_error.has_value());
    EXPECT_THAT(*parse_error, ::testing::HasSubstr(expected_error));
  }

  std::optional<std::string> ParseFrom(SandboxMetadata* sandbox, const std::string& json) {
    json::JSONParser parser;
    rapidjson::Document document = parser.ParseFromString(json, "test_file");
    EXPECT_FALSE(parser.HasError());
    EXPECT_TRUE(sandbox->IsNull());
    sandbox->Parse(document, &parser);
    if (parser.HasError()) {
      return parser.error_str();
    }
    return std::nullopt;
  }
};

TEST_F(SandboxMetadataTest, Parse) {
  // dev
  {
    const std::string json = R"JSON({
      "dev": [ "class/input" ]
    })JSON";
    SandboxMetadata sandbox;
    std::optional parse_error = ParseFrom(&sandbox, json);
    EXPECT_FALSE(parse_error.has_value()) << *parse_error;
    EXPECT_FALSE(sandbox.IsNull());
    EXPECT_THAT(sandbox.dev(), ::testing::ElementsAre("class/input"));
  }

  // system
  {
    const std::string json = R"JSON({
      "system": [ "data" ]
    })JSON";
    SandboxMetadata sandbox;
    std::optional parse_error = ParseFrom(&sandbox, json);
    EXPECT_FALSE(parse_error.has_value()) << *parse_error;
    EXPECT_FALSE(sandbox.IsNull());
    EXPECT_THAT(sandbox.system(), ::testing::ElementsAre("data"));
  }

  // services
  {
    const std::string json = R"JSON({
      "services": [ "fuchsia.sys.Launcher" ]
    })JSON";
    SandboxMetadata sandbox;
    std::optional parse_error = ParseFrom(&sandbox, json);
    EXPECT_FALSE(parse_error.has_value()) << *parse_error;
    EXPECT_FALSE(sandbox.IsNull());
    EXPECT_THAT(sandbox.services(), ::testing::ElementsAre("fuchsia.sys.Launcher"));
    EXPECT_TRUE(sandbox.HasService("fuchsia.sys.Launcher"));
    EXPECT_FALSE(sandbox.HasService("potato"));
  }

  // pkgfs
  {
    const std::string json = R"JSON({
      "pkgfs": [ "packages", "versions"]
    })JSON";
    SandboxMetadata sandbox;
    std::optional parse_error = ParseFrom(&sandbox, json);
    EXPECT_FALSE(parse_error.has_value()) << *parse_error;
    EXPECT_FALSE(sandbox.IsNull());
    EXPECT_THAT(sandbox.pkgfs(), ::testing::ElementsAre("packages", "versions"));
    EXPECT_TRUE(sandbox.HasPkgFsPath("packages"));
    EXPECT_TRUE(sandbox.HasPkgFsPath("versions"));
    EXPECT_FALSE(sandbox.HasPkgFsPath("ctl"));
  }

  // features
  {
    const std::string json = R"JSON({
      "features": [ "vulkan", "shell" ]
    })JSON";
    SandboxMetadata sandbox;
    std::optional parse_error = ParseFrom(&sandbox, json);
    EXPECT_FALSE(parse_error.has_value()) << *parse_error;
    EXPECT_FALSE(sandbox.IsNull());
    EXPECT_THAT(sandbox.features(), ::testing::ElementsAre("vulkan", "shell"));
    EXPECT_TRUE(sandbox.HasFeature("vulkan"));
    EXPECT_TRUE(sandbox.HasFeature("shell"));
    EXPECT_FALSE(sandbox.HasFeature("banana"));
  }

  // boot
  {
    const std::string json = R"JSON({
      "boot": [ "log" ]
    })JSON";
    SandboxMetadata sandbox;
    std::optional parse_error = ParseFrom(&sandbox, json);
    EXPECT_FALSE(parse_error.has_value()) << *parse_error;
    EXPECT_FALSE(sandbox.IsNull());
    EXPECT_THAT(sandbox.boot(), ::testing::ElementsAre("log"));
  }
}

TEST_F(SandboxMetadataTest, ParseWithErrors) {
  ExpectFailedParse(
      R"JSON({
        "dev": [ "class/input", 3 ]
      })JSON",
      "'dev' contains an item that's not a string");
}

}  // namespace
}  // namespace component
