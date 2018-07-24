// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace/spec.h"

#include "gtest/gtest.h"

namespace tracing {

namespace measure {

bool operator==(const measure::EventSpec& lhs, const measure::EventSpec& rhs) {
  return lhs.name == rhs.name && lhs.category == rhs.category;
}

bool operator==(const measure::DurationSpec& lhs,
                const measure::DurationSpec& rhs) {
  return lhs.id == rhs.id && lhs.event == rhs.event;
}

bool operator==(const measure::ArgumentValueSpec& lhs,
                const measure::ArgumentValueSpec& rhs) {
  return lhs.id == rhs.id && lhs.event == rhs.event;
}

bool operator==(const measure::TimeBetweenSpec& lhs,
                const measure::TimeBetweenSpec& rhs) {
  return lhs.id == rhs.id && lhs.first_event == rhs.first_event &&
         lhs.first_anchor == rhs.first_anchor &&
         lhs.second_event == rhs.second_event &&
         lhs.second_anchor == rhs.second_anchor;
}

}  // namespace measure

namespace {

TEST(Spec, DecodingErrors) {
  std::string json;
  Spec result;
  // Empty input.
  EXPECT_FALSE(DecodeSpec(json, &result));

  // Not an object.
  json = "[]";
  EXPECT_FALSE(DecodeSpec(json, &result));
  json = "yes";
  EXPECT_FALSE(DecodeSpec(json, &result));
  json = "4a";
  EXPECT_FALSE(DecodeSpec(json, &result));

  // Incorrect parameter types.
  json = R"({"app": 42})";
  EXPECT_FALSE(DecodeSpec(json, &result));
  json = R"({"args": "many"})";
  EXPECT_FALSE(DecodeSpec(json, &result));
  json = R"({"args": [42]})";
  EXPECT_FALSE(DecodeSpec(json, &result));
  json = R"({"categories": "many"})";
  EXPECT_FALSE(DecodeSpec(json, &result));
  json = R"({"categories": [42]})";
  EXPECT_FALSE(DecodeSpec(json, &result));
  json = R"({"duration": "long"})";
  EXPECT_FALSE(DecodeSpec(json, &result));
  json = R"({"measure": "yes"})";
  EXPECT_FALSE(DecodeSpec(json, &result));
  json = R"({"measure": [{"type": 42}]})";
  EXPECT_FALSE(DecodeSpec(json, &result));

  // Unknown measurement type.
  json = R"({"measure": [{"type": "unknown"}]})";
  EXPECT_FALSE(DecodeSpec(json, &result));

  // Missing measurement params.
  json = R"({"measure": [{"type": "duration"}]})";
  EXPECT_FALSE(DecodeSpec(json, &result));
  json = R"({"measure": [{"type": "time_between"}]})";
  EXPECT_FALSE(DecodeSpec(json, &result));

  // Additional properies.
  json = R"({"bla": "hey there"})";
  EXPECT_FALSE(DecodeSpec(json, &result));
}

TEST(Spec, DecodeEmpty) {
  std::string json = "{}";

  Spec result;
  ASSERT_TRUE(DecodeSpec(json, &result));
  EXPECT_EQ("", result.app);
  EXPECT_EQ(0u, result.measurements.duration.size());
  EXPECT_EQ(0u, result.measurements.time_between.size());
}

TEST(Spec, DecodeArgs) {
  std::string json = R"({"args": ["--flag", "positional"]})";

  Spec result;
  ASSERT_TRUE(DecodeSpec(json, &result));
  EXPECT_EQ(std::vector<std::string>({"--flag", "positional"}), result.args);
}

TEST(Spec, DecodeCategories) {
  std::string json = R"({"categories": ["c1", "c2"]})";

  Spec result;
  ASSERT_TRUE(DecodeSpec(json, &result));
  EXPECT_EQ(std::vector<std::string>({"c1", "c2"}), result.categories);
}

TEST(Spec, DecodeDuration) {
  std::string json = R"({"duration": 42})";

  Spec result;
  ASSERT_TRUE(DecodeSpec(json, &result));
  EXPECT_EQ(fxl::TimeDelta::FromSeconds(42).ToNanoseconds(),
            result.duration.ToNanoseconds());
}

TEST(Spec, DecodeTestSuiteName) {
  std::string json = R"({"test_suite_name": "test.suite"})";

  Spec result;
  ASSERT_TRUE(DecodeSpec(json, &result));
  EXPECT_EQ("test.suite", result.test_suite_name);
}

TEST(Spec, ErrorOnNegativeDuration) {
  std::string json = R"({"duration": -42})";

  Spec result;
  EXPECT_FALSE(DecodeSpec(json, &result));
}

TEST(Spec, DecodeMeasureDuration) {
  std::string json = R"({
    "measure":[
      {
        "type": "duration",
        "event_name": "initialization",
        "event_category": "bazinga"
      },
      {
        "type": "duration",
        "event_name": "startup",
        "event_category": "foo"
      }
    ]
  })";

  Spec result;
  ASSERT_TRUE(DecodeSpec(json, &result));
  EXPECT_EQ(2u, result.measurements.duration.size());
  EXPECT_EQ(measure::DurationSpec({0u, {"initialization", "bazinga"}}),
            result.measurements.duration[0]);
  EXPECT_EQ(measure::DurationSpec({1u, {"startup", "foo"}}),
            result.measurements.duration[1]);
}

TEST(Spec, DecodeMeasureArgumentValue) {
  std::string json = R"({
    "measure":[
      {
        "type": "argument_value",
        "event_name": "startup",
        "event_category": "foo",
        "argument_name": "disk_space",
        "argument_unit": "Mb"
      },
      {
        "type": "argument_value",
        "event_name": "shutdown",
        "event_category": "benchmark",
        "argument_name": "n_handles",
        "argument_unit": "handles"
      }
    ]
  })";

  Spec result;
  ASSERT_TRUE(DecodeSpec(json, &result));
  EXPECT_EQ(2u, result.measurements.argument_value.size());
  EXPECT_EQ(measure::ArgumentValueSpec({0u, {"startup", "foo"}, "bytes", "b"}),
            result.measurements.argument_value[0]);
  EXPECT_EQ(measure::ArgumentValueSpec(
                {1u, {"shutdown", "benchmark"}, "n_handles", "handles"}),
            result.measurements.argument_value[1]);
}

TEST(Spec, DecodeMeasureTimeBetween) {
  std::string json = R"({
    "measure": [
      {
        "type": "time_between",
        "first_event_name": "e1",
        "first_event_category": "c1",
        "first_event_anchor": "begin",
        "second_event_name": "e2",
        "second_event_category": "c2",
        "second_event_anchor": "end"
      }
    ]
  })";

  Spec result;
  ASSERT_TRUE(DecodeSpec(json, &result));
  EXPECT_EQ(1u, result.measurements.time_between.size());
  EXPECT_EQ(measure::TimeBetweenSpec({0u,
                                      {"e1", "c1"},
                                      measure::Anchor::Begin,
                                      {"e2", "c2"},
                                      measure::Anchor::End}),
            result.measurements.time_between[0]);
}

TEST(Spec, DecodeMeasurementSplitSamplesAt) {
  std::string json = R"({
    "measure": [
      {
        "type": "duration",
        "event_name": "foo",
        "event_category": "bar",
        "split_samples_at": [1,42]
      },
      {
        "type": "time_between",
        "first_event_name": "foo1",
        "first_event_category": "bar1",
        "second_event_name": "foo2",
        "second_event_category": "bar2",
        "split_samples_at": [2]
      }
    ]
  })";

  Spec spec;
  ASSERT_TRUE(DecodeSpec(json, &spec));
  auto measurements = std::move(spec.measurements);
  EXPECT_EQ(1u, measurements.duration.size());
  auto expected = std::unordered_map<uint64_t, std::vector<size_t>>{
      {0u, {1u, 42u}}, {1u, {2u}}};
  EXPECT_EQ(expected, measurements.split_samples_at);
}

TEST(Spec, DecodeMeasurementExpectedSampleCount) {
  std::string json = R"({
    "measure": [
      {
        "type": "duration",
        "expected_sample_count": 10,
        "event_name": "foo",
        "event_category": "bar"
      },
      {
        "type": "duration",
        "event_name": "foz",
        "event_category": "baz"
      }
    ]
  })";

  Spec spec;
  ASSERT_TRUE(DecodeSpec(json, &spec));
  auto measurements = std::move(spec.measurements);
  EXPECT_EQ(2u, measurements.duration.size());
  auto expected = std::unordered_map<uint64_t, size_t>{{0u, {10u}}};
  EXPECT_EQ(expected, measurements.expected_sample_count);
}

}  // namespace

}  // namespace tracing
