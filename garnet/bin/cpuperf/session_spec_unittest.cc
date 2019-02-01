// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "garnet/lib/cpuperf/events.h"

#include "session_spec.h"

namespace cpuperf {

namespace {

TEST(SessionSpec, DecodingErrors) {
  std::string json;
  SessionSpec result;

  // Empty input.
  EXPECT_FALSE(DecodeSessionSpec(json, &result));

  // Not an object.
  json = "[]";
  EXPECT_FALSE(DecodeSessionSpec(json, &result));
  json = "yes";
  EXPECT_FALSE(DecodeSessionSpec(json, &result));
  json = "4a";
  EXPECT_FALSE(DecodeSessionSpec(json, &result));

  // Incorrect parameter types.
  json = R"({"config_name": 42})";
  EXPECT_FALSE(DecodeSessionSpec(json, &result));

  json = R"({"events": 42})";
  EXPECT_FALSE(DecodeSessionSpec(json, &result));

  json = R"({"buffer_size_in_mb": "yikes"})";
  EXPECT_FALSE(DecodeSessionSpec(json, &result));

  json = R"({"duration": "long"})";
  EXPECT_FALSE(DecodeSessionSpec(json, &result));
  json = R"({"duration": -42})";
  EXPECT_FALSE(DecodeSessionSpec(json, &result));

  json = R"({"num_iterations": false})";
  EXPECT_FALSE(DecodeSessionSpec(json, &result));

  // Bad buffer size.
  json = R"({"buffer_size_in_mb": 0})";
  EXPECT_FALSE(DecodeSessionSpec(json, &result));

  // Additional properies.
  json = R"({"bla": "hey there"})";
  EXPECT_FALSE(DecodeSessionSpec(json, &result));
}

TEST(SessionSpec, DecodeConfigName) {
  std::string json = R"({"config_name": "test"})";

  SessionSpec result;
  ASSERT_TRUE(DecodeSessionSpec(json, &result));
  EXPECT_EQ("test", result.config_name);
}

TEST(SessionSpec, DecodeEvents) {
  std::string json = R"({"events":
  [
    {
      "group_name": "fixed",
      "event_name": "instructions_retired",
      "rate": 42,
      "flags": [ "os", "user", "pc", "timebase0" ]
    }
  ]
})";

  SessionSpec result;
  ASSERT_TRUE(DecodeSessionSpec(json, &result));

  const EventDetails* details;
  ASSERT_TRUE(cpuperf::LookupEventByName("fixed", "instructions_retired",
                                         &details));
  cpuperf_event_id_t instructions_retired_id = details->id;

  EXPECT_EQ(result.cpuperf_config.events[0], instructions_retired_id);
  EXPECT_EQ(result.cpuperf_config.rate[0], 42u);
  EXPECT_EQ(result.cpuperf_config.flags[0],
            CPUPERF_CONFIG_FLAG_OS |
            CPUPERF_CONFIG_FLAG_USER |
            CPUPERF_CONFIG_FLAG_PC |
            CPUPERF_CONFIG_FLAG_TIMEBASE0);
  for (size_t i = 1; i < CPUPERF_MAX_EVENTS; ++i) {
    EXPECT_EQ(result.cpuperf_config.events[i], 0);
  }
}

TEST(SessionSpec, DecodeBufferSizeInMb) {
  std::string json = R"({"buffer_size_in_mb": 1})";

  SessionSpec result;
  ASSERT_TRUE(DecodeSessionSpec(json, &result));
  EXPECT_EQ(1u, result.buffer_size_in_mb);
}

TEST(SessionSpec, DecodeDuration) {
  std::string json = R"({"duration": 42})";

  SessionSpec result;
  ASSERT_TRUE(DecodeSessionSpec(json, &result));
  EXPECT_EQ(fxl::TimeDelta::FromSeconds(42).ToNanoseconds(),
            result.duration.ToNanoseconds());
}

TEST(SessionSpec, DecodeNumIterations) {
  std::string json = R"({"num_iterations": 99})";

  SessionSpec result;
  ASSERT_TRUE(DecodeSessionSpec(json, &result));
  EXPECT_EQ(99u, result.num_iterations);
}

}  // namespace

}  // namespace cpuperf
