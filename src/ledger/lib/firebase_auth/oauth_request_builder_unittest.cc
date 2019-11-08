// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/firebase_auth/oauth_request_builder.h"

#include <string>

#include "gtest/gtest.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "src/lib/fxl/logging.h"

namespace firebase_auth {

namespace {

constexpr char kTestUrl[] = "http://example.org";
constexpr char kPostMethod[] = "POST";
constexpr char kGetMethod[] = "GET";

class OAuthRequestBuilderTest : public ::testing::Test {
 protected:
  OAuthRequestBuilderTest() {}

  ~OAuthRequestBuilderTest() override {}
};

TEST_F(OAuthRequestBuilderTest, JsonEncodedPostRequest) {
  rapidjson::Document json_doc;
  json_doc.Parse(R"({"test_key":"test_val"})");

  // convert json document to string
  rapidjson::StringBuffer strbuf;
  strbuf.Clear();
  rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
  json_doc.Accept(writer);

  auto req = OAuthRequestBuilder(kTestUrl, kPostMethod).SetJsonBody(strbuf.GetString()).Build();

  EXPECT_TRUE(req.url.find("example.org") != std::string::npos);
  EXPECT_EQ(req.method, kPostMethod);
  for (const auto& header : *req.headers) {
    auto hdr_name = header.name;
    if (hdr_name == "content-type") {
      EXPECT_EQ(header.value, "application/json");
    } else if (hdr_name == "content-length") {
      EXPECT_TRUE(atoi(header.value.c_str()) > 0);
    }
  }
}

TEST_F(OAuthRequestBuilderTest, GetRequest) {
  auto req = OAuthRequestBuilder(kTestUrl, kGetMethod).Build();

  EXPECT_TRUE(req.url.find("example.org") != std::string::npos);
  EXPECT_EQ(req.method, kGetMethod);
}

TEST_F(OAuthRequestBuilderTest, GetRequestWithQueryParams) {
  std::map<std::string, std::string> params;
  params["foo1"] = "bar1";
  params["foo2"] = "bar2";
  params["foo3"] = "bar 3";
  auto req = OAuthRequestBuilder(kTestUrl, kGetMethod).SetQueryParams(params).Build();

  EXPECT_TRUE(req.url.find("example.org") != std::string::npos);
  EXPECT_TRUE(req.url.find("foo1") != std::string::npos);
  EXPECT_TRUE(req.url.find("foo2") != std::string::npos);
  // check if the param values are url encoded
  EXPECT_TRUE(req.url.find("bar%203") != std::string::npos);
  EXPECT_EQ(req.method, kGetMethod);
}

class FirebaseRequestBuilderTest : public ::testing::Test {
 protected:
  FirebaseRequestBuilderTest() {}

  ~FirebaseRequestBuilderTest() override {}
};

TEST_F(FirebaseRequestBuilderTest, Success) {
  auto req = FirebaseRequestBuilder("foo321", "bar123").Build();

  EXPECT_TRUE(req.url.find("foo321") != std::string::npos);
  EXPECT_FALSE(req.body == nullptr);
}

}  // namespace

}  // namespace firebase_auth
