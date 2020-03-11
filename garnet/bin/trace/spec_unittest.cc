// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "garnet/bin/trace/spec.h"
#include "garnet/lib/measure/results.h"

namespace tracing {

namespace measure {

bool operator==(const measure::EventSpec& lhs, const measure::EventSpec& rhs) {
  return lhs.name == rhs.name && lhs.category == rhs.category;
}

bool operator==(const measure::DurationSpec& lhs, const measure::DurationSpec& rhs) {
  return lhs.common.id == rhs.common.id && lhs.event == rhs.event;
}

bool operator==(const measure::ArgumentValueSpec& lhs, const measure::ArgumentValueSpec& rhs) {
  return lhs.common.id == rhs.common.id && lhs.event == rhs.event;
}

bool operator==(const measure::TimeBetweenSpec& lhs, const measure::TimeBetweenSpec& rhs) {
  return lhs.common.id == rhs.common.id && lhs.first_event == rhs.first_event &&
         lhs.first_anchor == rhs.first_anchor && lhs.second_event == rhs.second_event &&
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
  json = R"({"test_name": 42})";
  EXPECT_FALSE(DecodeSpec(json, &result));
  json = R"({"app": 42})";
  EXPECT_FALSE(DecodeSpec(json, &result));
  json = R"({"args": "many"})";
  EXPECT_FALSE(DecodeSpec(json, &result));
  json = R"({"args": [42]})";
  EXPECT_FALSE(DecodeSpec(json, &result));
  json = R"({"spawn": "yikes"})";
  EXPECT_FALSE(DecodeSpec(json, &result));
  json = R"({"environment": "yikes"})";
  EXPECT_FALSE(DecodeSpec(json, &result));
  json = R"({"environment": {}})";
  EXPECT_FALSE(DecodeSpec(json, &result));
  json = R"({"environment": {"name": 42}})";
  EXPECT_FALSE(DecodeSpec(json, &result));
  json = R"({"categories": "many"})";
  EXPECT_FALSE(DecodeSpec(json, &result));
  json = R"({"categories": [42]})";
  EXPECT_FALSE(DecodeSpec(json, &result));
  json = R"({"buffering_mode": 42})";
  EXPECT_FALSE(DecodeSpec(json, &result));
  json = R"({"buffer_size_in_mb": "yikes"})";
  EXPECT_FALSE(DecodeSpec(json, &result));
  json = R"({"duration": "long"})";
  EXPECT_FALSE(DecodeSpec(json, &result));
  json = R"({"measure": "yes"})";
  EXPECT_FALSE(DecodeSpec(json, &result));
  json = R"({"measure": [{"type": 42}]})";
  EXPECT_FALSE(DecodeSpec(json, &result));

  // Bad buffer size.
  json = R"({"buffer_size_in_mb": 0})";
  EXPECT_FALSE(DecodeSpec(json, &result));

  // Missing name in provider spec.
  json = R"({"provider_specs": [{"buffer_size_in_mb": 1}]})";
  EXPECT_FALSE(DecodeSpec(json, &result));

  // Bad buffer size in provider spec.
  json = R"({"provider_specs": [{"name": "x", "buffer_size_in_mb": 0}]})";
  EXPECT_FALSE(DecodeSpec(json, &result));

  // Bad field in provider spec.
  json = R"({"provider_specs": [{"name": "x", "garbage": 0}]})";
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
  EXPECT_FALSE(result.test_name);
  EXPECT_FALSE(result.app);
  EXPECT_FALSE(result.args);
  EXPECT_FALSE(result.spawn);
  EXPECT_FALSE(result.environment_name);
  EXPECT_FALSE(result.categories);
  EXPECT_FALSE(result.buffering_mode);
  EXPECT_FALSE(result.buffer_size_in_mb);
  EXPECT_FALSE(result.duration);
  EXPECT_FALSE(result.measurements);
  EXPECT_FALSE(result.test_suite_name);
}

TEST(Spec, DecodeTestName) {
  std::string json = R"({"test_name": "test"})";

  Spec result;
  ASSERT_TRUE(DecodeSpec(json, &result));
  EXPECT_EQ("test", *result.test_name);
  EXPECT_TRUE(result.test_name);
}

TEST(Spec, DecodeArgs) {
  std::string json = R"({"args": ["--flag", "positional"]})";

  Spec result;
  ASSERT_TRUE(DecodeSpec(json, &result));
  EXPECT_EQ(std::vector<std::string>({"--flag", "positional"}), *result.args);
  EXPECT_TRUE(result.args);
}

TEST(Spec, DecodeSpawn) {
  {
    std::string json = R"({"spawn": false})";
    Spec result;
    ASSERT_TRUE(DecodeSpec(json, &result));
    EXPECT_FALSE(*result.spawn);
    EXPECT_TRUE(result.spawn);
  }
  {
    std::string json = R"({"spawn": true})";
    Spec result;
    ASSERT_TRUE(DecodeSpec(json, &result));
    EXPECT_TRUE(*result.spawn);
    EXPECT_TRUE(result.spawn);
  }
}

TEST(Spec, DecodeEnvironment) {
  std::string json = R"({"environment": {"name": "env_name"}})";
  Spec result;
  ASSERT_TRUE(DecodeSpec(json, &result));
  ASSERT_TRUE(result.environment_name);
  EXPECT_EQ("env_name", *result.environment_name);
}

TEST(Spec, DecodeCategories) {
  std::string json = R"({"categories": ["c1", "c2"]})";

  Spec result;
  ASSERT_TRUE(DecodeSpec(json, &result));
  EXPECT_EQ(std::vector<std::string>({"c1", "c2"}), *result.categories);
  EXPECT_TRUE(result.categories);
}

TEST(Spec, DecodeBufferingMode) {
  {
    std::string json = R"({"buffering_mode": "oneshot"})";
    Spec result;
    ASSERT_TRUE(DecodeSpec(json, &result));
    EXPECT_EQ("oneshot", *result.buffering_mode);
    EXPECT_TRUE(result.buffering_mode);
  }
  {
    std::string json = R"({"buffering_mode": "circular"})";
    Spec result;
    ASSERT_TRUE(DecodeSpec(json, &result));
    EXPECT_EQ("circular", *result.buffering_mode);
    EXPECT_TRUE(result.buffering_mode);
  }
  {
    std::string json = R"({"buffering_mode": "streaming"})";
    Spec result;
    ASSERT_TRUE(DecodeSpec(json, &result));
    EXPECT_EQ("streaming", *result.buffering_mode);
    EXPECT_TRUE(result.buffering_mode);
  }
}

TEST(Spec, DecodeBufferSizeInMb) {
  std::string json = R"({"buffer_size_in_mb": 1})";

  Spec result;
  ASSERT_TRUE(DecodeSpec(json, &result));
  EXPECT_EQ(1u, *result.buffer_size_in_mb);
  EXPECT_TRUE(result.buffer_size_in_mb);
}

TEST(Spec, DecodeProviderSpecs) {
  std::string json = R"({"provider_specs": [)"
                     R"(  {"name": "x", "buffer_size_in_mb": 1},)"
                     R"(  {"name": "y", "buffer_size_in_mb": 2})"
                     R"(]})";

  Spec result;
  ASSERT_TRUE(DecodeSpec(json, &result));
  ASSERT_TRUE(result.provider_specs);
  ASSERT_EQ(result.provider_specs->size(), 2u);
  EXPECT_EQ((*result.provider_specs)[0].name, "x");
  EXPECT_EQ((*result.provider_specs)[0].buffer_size_in_mb, 1u);
  EXPECT_EQ((*result.provider_specs)[1].name, "y");
  EXPECT_EQ((*result.provider_specs)[1].buffer_size_in_mb, 2u);
}

TEST(Spec, DecodeDuration) {
  std::string json = R"({"duration": 42})";

  Spec result;
  ASSERT_TRUE(DecodeSpec(json, &result));
  EXPECT_EQ(zx::sec(42).to_nsecs(), result.duration->to_nsecs());
  EXPECT_TRUE(result.duration);
}

TEST(Spec, DecodeTestSuiteName) {
  std::string json = R"({"test_suite_name": "test.suite"})";

  Spec result;
  ASSERT_TRUE(DecodeSpec(json, &result));
  EXPECT_EQ("test.suite", *result.test_suite_name);
  EXPECT_TRUE(result.test_suite_name);
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
  EXPECT_EQ(2u, result.measurements->duration.size());
  EXPECT_EQ(measure::DurationSpec({0u, {"initialization", "bazinga"}}),
            result.measurements->duration[0]);
  EXPECT_EQ(measure::DurationSpec({1u, {"startup", "foo"}}), result.measurements->duration[1]);
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
  EXPECT_EQ(2u, result.measurements->argument_value.size());
  EXPECT_EQ(measure::ArgumentValueSpec({0u, {"startup", "foo"}, "bytes", "b"}),
            result.measurements->argument_value[0]);
  EXPECT_EQ(measure::ArgumentValueSpec({1u, {"shutdown", "benchmark"}, "n_handles", "handles"}),
            result.measurements->argument_value[1]);
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
  EXPECT_EQ(1u, result.measurements->time_between.size());
  EXPECT_EQ(measure::TimeBetweenSpec(
                {0u, {"e1", "c1"}, measure::Anchor::Begin, {"e2", "c2"}, measure::Anchor::End}),
            result.measurements->time_between[0]);
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
  auto measurements = std::move(*spec.measurements);
  EXPECT_EQ(2u, measurements.duration.size());
  EXPECT_EQ(10u, measurements.duration[0].common.expected_sample_count);
  EXPECT_EQ(0u, measurements.duration[1].common.expected_sample_count);
}

TEST(Spec, DecodeMeasurementSplitFirst) {
  std::string json = R"({
    "measure": [
      {
        "type": "duration",
        "split_first": true,
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
  auto measurements = std::move(*spec.measurements);
  EXPECT_EQ(2u, measurements.duration.size());
  EXPECT_EQ(true, measurements.duration[0].common.split_first);
  EXPECT_EQ(false, measurements.duration[1].common.split_first);
}

// Test the test case name that gets generated from event_name and
// event_category.
TEST(Spec, OutputNameDefault) {
  std::string json = R"({
    "measure": [
      {
        "type": "duration",
        "event_name": "test_event",
        "event_category": "test_category"
      }
    ]
  })";

  Spec spec;
  ASSERT_TRUE(DecodeSpec(json, &spec));
  auto measurements = std::move(*spec.measurements);
  std::unordered_map<uint64_t, std::vector<trace_ticks_t>> ticks;
  auto results = tracing::measure::ComputeResults(measurements, ticks, 1000.0);
  EXPECT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].label, "test_event (test_category)");
}

// Test overriding the test case name using the "output_test_name" field.
TEST(Spec, OutputNameOverride) {
  std::string json = R"({
    "measure": [
      {
        "type": "duration",
        "event_name": "test_event",
        "event_category": "test_category",
        "output_test_name": "my_test_name"
      }
    ]
  })";

  Spec spec;
  ASSERT_TRUE(DecodeSpec(json, &spec));
  auto measurements = std::move(*spec.measurements);
  std::unordered_map<uint64_t, std::vector<trace_ticks_t>> ticks;
  auto results = tracing::measure::ComputeResults(measurements, ticks, 1000.0);
  EXPECT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].label, "my_test_name");
}

}  // namespace

}  // namespace tracing
