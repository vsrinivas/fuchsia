// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/cmx/facets.h"

#include <string>

#include "gtest/gtest.h"

#include "garnet/lib/json/json_parser.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace component {
namespace {

class FacetsMetadataTest : public ::testing::Test {
 protected:
  bool ParseFrom(FacetsMetadata* facets, const std::string& json,
                 std::string* error) {
    json::JSONParser parser;
    rapidjson::Document document = parser.ParseFromString(json, "test_file");
    EXPECT_FALSE(parser.HasError());
    const bool ret = facets->Parse(document, &parser);
    if (parser.HasError()) {
      *error = parser.error_str();
    }
    return ret;
  }
};

TEST_F(FacetsMetadataTest, ParseEmpty) {
  FacetsMetadata facets;
  std::string error;
  EXPECT_TRUE(ParseFrom(&facets, R"JSON({})JSON", &error));
}

TEST_F(FacetsMetadataTest, ParseSection) {
  FacetsMetadata facets;
  std::string error;
  EXPECT_TRUE(ParseFrom(&facets, R"JSON({"section1":
      { "v1" : 1, "v2" : 2, "v3": "v3_value"
      }, "section2": "some_str"})JSON",
                        &error));
  const auto& section1 = facets.GetSection("section1");
  ASSERT_FALSE(section1.IsNull());
  ASSERT_TRUE(section1.HasMember("v1"));
  ASSERT_TRUE(section1.HasMember("v2"));
  ASSERT_TRUE(section1.HasMember("v3"));
  EXPECT_EQ(section1["v1"].GetInt(), 1);
  EXPECT_EQ(section1["v2"].GetInt(), 2);
  EXPECT_EQ(std::string(section1["v3"].GetString()), "v3_value");

  const auto& section2 = facets.GetSection("section2");
  ASSERT_FALSE(section2.IsNull());
  ASSERT_TRUE(section2.IsString());
  EXPECT_EQ(std::string(section2.GetString()), "some_str");

  const auto& invalid = facets.GetSection("invalid");
  ASSERT_TRUE(invalid.IsNull());
}

}  // namespace
}  // namespace component
