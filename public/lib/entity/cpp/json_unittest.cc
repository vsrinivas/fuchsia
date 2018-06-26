// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/public/lib/entity/cpp/json.h"

#include <string>

#include "gtest/gtest.h"
#include "rapidjson/document.h"

namespace modular {

TEST(EntityJsonTest, EntityReferenceToJson) {
  auto ret = EntityReferenceToJson("reference");
  EXPECT_EQ(R"({"@entityRef":"reference"})", ret);
}

TEST(EntityJsonTest, EntityReferenceToJsonDoc) {
  auto ret = EntityReferenceToJsonDoc("reference");
  EXPECT_TRUE(ret.IsObject());
  EXPECT_TRUE(ret.HasMember("@entityRef"));
  EXPECT_EQ("reference", std::string(ret["@entityRef"].GetString()));
}

TEST(EntityJsonTest, EntityReferenceFromJson) {
  std::string ret;
  EXPECT_FALSE(EntityReferenceFromJson("invalid", &ret));
  EXPECT_FALSE(EntityReferenceFromJson("1", &ret));
  EXPECT_FALSE(EntityReferenceFromJson(R"({"@entityRoof":"reference"})", &ret));
  EXPECT_FALSE(EntityReferenceFromJson(R"({"@entityRef":12345})", &ret));
  EXPECT_FALSE(
      EntityReferenceFromJson(R"({"@entityRef":["reference"]})", &ret));

  EXPECT_TRUE(EntityReferenceFromJson(R"({"@entityRef":"reference"})", &ret));
  EXPECT_EQ("reference", ret);

  // EntityReferenceFromJson(rapidjson::Value) is tested implicitly with the
  // above.
}

TEST(EntityJsonTest, ExtractEntityTypesFromJson) {
  std::vector<std::string> types;
  // Null cases.
  EXPECT_FALSE(ExtractEntityTypesFromJson("1", &types));
  EXPECT_FALSE(ExtractEntityTypesFromJson("[1,2,3]", &types));
  EXPECT_FALSE(ExtractEntityTypesFromJson("{}", &types));
  EXPECT_FALSE(ExtractEntityTypesFromJson(R"({"type": "foo"})", &types));
  EXPECT_FALSE(ExtractEntityTypesFromJson(R"({"@type": 1})", &types));
  EXPECT_FALSE(ExtractEntityTypesFromJson(R"({"@type": {}})", &types));
  EXPECT_FALSE(ExtractEntityTypesFromJson(R"({"@type": [1,"foo"]})", &types));

  // JSON string case.
  EXPECT_TRUE(ExtractEntityTypesFromJson(R"("hello")", &types));
  EXPECT_EQ(1lu, types.size());
  EXPECT_EQ("com.google.fuchsia.string", types[0]);
  types.clear();

  // Explicit "@type" case.
  EXPECT_TRUE(ExtractEntityTypesFromJson(R"({"@type": "foo"})", &types));
  EXPECT_EQ(1lu, types.size());
  EXPECT_EQ("foo", types[0]);
  types.clear();

  EXPECT_TRUE(ExtractEntityTypesFromJson(R"({"@type":["foo","bar"]})", &types));
  EXPECT_EQ(2lu, types.size());
  EXPECT_EQ("foo", types[0]);
  EXPECT_EQ("bar", types[1]);
  types.clear();
}

}  // namespace modular
