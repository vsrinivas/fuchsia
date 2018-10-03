// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/cmx/sandbox.h"

#include <string>

#include "garnet/lib/json/json_parser.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "rapidjson/document.h"

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
    EXPECT_THAT(error, ::testing::HasSubstr(expected_error));
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
  // dev
  {
    const std::string json = R"JSON({
      "dev": [ "class/input" ],
      "services": []
    })JSON";
    SandboxMetadata sandbox;
    std::string error;
    EXPECT_TRUE(ParseFrom(&sandbox, json, &error));
    EXPECT_EQ(error, "");
    EXPECT_FALSE(sandbox.IsNull());
    EXPECT_THAT(sandbox.dev(), ::testing::ElementsAre("class/input"));
  }

  // system
  {
    const std::string json = R"JSON({
      "system": [ "data" ],
      "services": []
    })JSON";
    SandboxMetadata sandbox;
    std::string error;
    EXPECT_TRUE(ParseFrom(&sandbox, json, &error));
    EXPECT_EQ(error, "");
    EXPECT_FALSE(sandbox.IsNull());
    EXPECT_THAT(sandbox.system(), ::testing::ElementsAre("data"));
    EXPECT_FALSE(sandbox.has_all_services());
  }

  // services
  {
    const std::string json = R"JSON({
      "services": [ "fuchsia.sys.Launcher" ]
    })JSON";
    SandboxMetadata sandbox;
    std::string error;
    EXPECT_TRUE(ParseFrom(&sandbox, json, &error));
    EXPECT_EQ(error, "");
    EXPECT_FALSE(sandbox.IsNull());
    EXPECT_THAT(sandbox.services(),
                ::testing::ElementsAre("fuchsia.sys.Launcher"));
    EXPECT_FALSE(sandbox.has_all_services());
  }

  // deprecated-all-services
  {
    SandboxMetadata sandbox;
    std::string error;
    EXPECT_TRUE(ParseFrom(
        &sandbox, R"JSON({ "features": [ "deprecated-all-services" ] })JSON",
        &error));
    EXPECT_EQ(error, "");
    EXPECT_FALSE(sandbox.IsNull());
    EXPECT_THAT(sandbox.services(), ::testing::ElementsAre());
    EXPECT_TRUE(sandbox.has_all_services());
  }

  // pkgfs
  {
    const std::string json = R"JSON({
      "pkgfs": [ "packages" ],
      "services": []
    })JSON";
    SandboxMetadata sandbox;
    std::string error;
    EXPECT_TRUE(ParseFrom(&sandbox, json, &error));
    EXPECT_EQ(error, "");
    EXPECT_FALSE(sandbox.IsNull());
    EXPECT_THAT(sandbox.pkgfs(), ::testing::ElementsAre("packages"));
  }

  // features
  {
    const std::string json = R"JSON({
      "features": [ "vulkan", "shell" ],
      "services": []
    })JSON";
    SandboxMetadata sandbox;
    std::string error;
    EXPECT_TRUE(ParseFrom(&sandbox, json, &error));
    EXPECT_EQ(error, "");
    EXPECT_FALSE(sandbox.IsNull());
    EXPECT_THAT(sandbox.features(), ::testing::ElementsAre("vulkan", "shell"));
    EXPECT_TRUE(sandbox.HasFeature("vulkan"));
    EXPECT_TRUE(sandbox.HasFeature("shell"));
    EXPECT_FALSE(sandbox.HasFeature("banana"));
  }

  // boot
  {
    const std::string json = R"JSON({
      "boot": [ "log" ],
      "services": []
    })JSON";
    SandboxMetadata sandbox;
    std::string error;
    EXPECT_TRUE(ParseFrom(&sandbox, json, &error));
    EXPECT_EQ(error, "");
    EXPECT_FALSE(sandbox.IsNull());
    EXPECT_THAT(sandbox.boot(), ::testing::ElementsAre("log"));
  }
}

#define SERVICES_INFO                                        \
  "\nRefer to "                                              \
  "https://fuchsia.googlesource.com/docs/+/master/the-book/" \
  "package_metadata.md#sandbox for more information."

TEST_F(SandboxMetadataTest, ParseWithErrors) {
  ExpectFailedParse(
      R"JSON({
        "dev": [ "class/input", 3 ]
      })JSON",
      "Entry for 'dev' in sandbox is not a string.");
  ExpectFailedParse(
      R"JSON({
        "features": [ "vulkan", "deprecated-all-services" ],
        "services": [ "fuchsia.sys.Launcher" ]
      })JSON",
      "Sandbox may not include both 'services' and "
      "'deprecated-all-services'." SERVICES_INFO);
  ExpectFailedParse(
      R"JSON({
        "features": [ "vulkan" ]
      })JSON",
      "Sandbox must include either 'services' or "
      "'deprecated-all-services'." SERVICES_INFO);
}

}  // namespace
}  // namespace component
