// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "session_result_spec.h"

namespace cpuperf {

namespace {

TEST(SessionResultSpec, DecodingErrors) {
  std::string json;
  SessionResultSpec result;

  // Empty input.
  EXPECT_FALSE(DecodeSessionResultSpec(json, &result));

  // Not an object.
  json = "[]";
  EXPECT_FALSE(DecodeSessionResultSpec(json, &result));
  json = "yes";
  EXPECT_FALSE(DecodeSessionResultSpec(json, &result));
  json = "4a";
  EXPECT_FALSE(DecodeSessionResultSpec(json, &result));

  // Incorrect parameter types.
  json = R"({"config_name": 42})";
  EXPECT_FALSE(DecodeSessionResultSpec(json, &result));

  json = R"({"model_name": 42})";
  EXPECT_FALSE(DecodeSessionResultSpec(json, &result));

  json = R"({"num_iterations": false})";
  EXPECT_FALSE(DecodeSessionResultSpec(json, &result));

  json = R"({"num_traces": "bleah"})";
  EXPECT_FALSE(DecodeSessionResultSpec(json, &result));

  // Incorrect parameter types.
  json = R"({"output_path_prefix": 42})";
  EXPECT_FALSE(DecodeSessionResultSpec(json, &result));

  // Incorrect parameter types.
  json = R"({"output_files": 12.34})";
  EXPECT_FALSE(DecodeSessionResultSpec(json, &result));

  // Additional properies.
  json = R"({"bla": "hey there"})";
  EXPECT_FALSE(DecodeSessionResultSpec(json, &result));
}

TEST(SessionResultSpec, DecodeConfigName) {
  std::string json = R"({"config_name": "test"})";

  SessionResultSpec result;
  ASSERT_TRUE(DecodeSessionResultSpec(json, &result));
  EXPECT_EQ("test", result.config_name);
}

TEST(SessionResultSpec, DecodeModelName) {
  std::string json = R"({"model_name": "test"})";

  SessionResultSpec result;
  ASSERT_TRUE(DecodeSessionResultSpec(json, &result));
  EXPECT_EQ("test", result.model_name);
}

TEST(SessionResultSpec, DecodeNumIterations) {
  std::string json = R"({"num_iterations": 99})";

  SessionResultSpec result;
  ASSERT_TRUE(DecodeSessionResultSpec(json, &result));
  EXPECT_EQ(99u, result.num_iterations);
}

TEST(SessionResultSpec, DecodeNumTraces) {
  std::string json = R"({"num_iterations": 8})";

  SessionResultSpec result;
  ASSERT_TRUE(DecodeSessionResultSpec(json, &result));
  EXPECT_EQ(8u, result.num_iterations);
}

TEST(SessionResultSpec, DecodeOutputPathPrefix) {
  std::string json = R"({"output_path_prefix": "/tmp/test"})";

  SessionResultSpec result;
  ASSERT_TRUE(DecodeSessionResultSpec(json, &result));
  EXPECT_EQ("/tmp/test", result.output_path_prefix);
}

}  // namespace

}  // namespace cpuperf
