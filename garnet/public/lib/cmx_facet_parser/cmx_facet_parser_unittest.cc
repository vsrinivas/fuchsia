// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/cmx_facet_parser/cmx_facet_parser.h"

#include <fcntl.h>
#include <string>
#include <tuple>

#include "gtest/gtest.h"

#include "src/lib/files/scoped_temp_dir.h"
#include "lib/json/json_parser.h"
#include "rapidjson/document.h"

namespace component {
namespace {

class CmxFacetParserTest : public ::testing::Test {
 protected:
  // Returns true for a valid cmx manifest represented by |json|.
  bool ParseFrom(const std::string& json) {
    json::JSONParser json_parser;
    rapidjson::Document document = json_parser.ParseFromString(json, "test_file");
    EXPECT_FALSE(json_parser.HasError());

    EXPECT_TRUE(facet_parser().Parse(document, &json_parser));
    return !json_parser.HasError();
  }

  bool ParseFromFileAt(const std::string& json) {
    files::ScopedTempDir tmp_dir;
    const int dirfd = open(tmp_dir.path().c_str(), O_RDONLY);

    std::string tmp_file;
    tmp_dir.NewTempFileWithData(json, &tmp_file);

    json::JSONParser json_parser;
    return facet_parser().ParseFromFileAt(dirfd, tmp_file, &json_parser);
  }

  CmxFacetParser& facet_parser() { return facet_parser_; };

 private:
  CmxFacetParser facet_parser_;
};

TEST_F(CmxFacetParserTest, ParseEmpty) { EXPECT_TRUE(ParseFrom(R"JSON({})JSON")); }

TEST_F(CmxFacetParserTest, ParseSection) {
  EXPECT_TRUE(ParseFrom(R"JSON({"facets":
      {"section1":
        { "v1" : 1, "v2" : 2, "v3": "v3_value" },
      "section2": "some_str"}
    })JSON"));

  const auto& section1 = facet_parser().GetSection("section1");
  ASSERT_FALSE(section1.IsNull());
  ASSERT_TRUE(section1.HasMember("v1"));
  ASSERT_TRUE(section1.HasMember("v2"));
  ASSERT_TRUE(section1.HasMember("v3"));
  EXPECT_EQ(section1["v1"].GetInt(), 1);
  EXPECT_EQ(section1["v2"].GetInt(), 2);
  EXPECT_EQ(std::string(section1["v3"].GetString()), "v3_value");

  const auto& section2 = facet_parser().GetSection("section2");
  ASSERT_FALSE(section2.IsNull());
  ASSERT_TRUE(section2.IsString());
  EXPECT_EQ(std::string(section2.GetString()), "some_str");

  const auto& invalid = facet_parser().GetSection("invalid");
  ASSERT_TRUE(invalid.IsNull());
}

TEST_F(CmxFacetParserTest, ParseFromFileAt) {
  files::ScopedTempDir tmp_dir;
  const int dirfd = open(tmp_dir.path().c_str(), O_RDONLY);

  std::string tmp_file;
  tmp_dir.NewTempFileWithData(R"JSON({"facets":
      {"section1":
        { "v1" : 1, "v2" : 2, "v3": "v3_value" },
      "section2": "some_str"}
    })JSON",
                              &tmp_file);

  json::JSONParser json_parser;
  EXPECT_TRUE(facet_parser().ParseFromFileAt(dirfd, tmp_file, &json_parser));
  EXPECT_FALSE(json_parser.HasError());
}

}  // namespace
}  // namespace component
