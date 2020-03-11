// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "session_spec.h"

#include <gtest/gtest.h>

#include "garnet/lib/perfmon/events.h"
#include "src/lib/fxl/arraysize.h"

namespace cpuperf {

namespace {

class SessionSpecTest : public ::testing::Test {
 public:
  // ::testing::Test overrides
  void SetUp() override { RegisterTestModel(); }
  void TearDown() override {}

 private:
  // Ensure there's a model named "test".
  // This serves two functions:
  // - provide a model to test against
  // - cause |ModelEventManager::Create()| to not register events for the
  //   current arch
  void RegisterTestModel() {
    // Only do this once though, the effect is otherwise cumulative.
    if (!models_registered_) {
      static const perfmon::EventDetails TestEvents[] = {
          {perfmon::MakeEventId(perfmon::kGroupFixed, 1), "test-event", "test-event",
           "test-event description"},
      };
      perfmon::ModelEventManager::RegisterEvents("test", "misc", &TestEvents[0],
                                                 arraysize(TestEvents));
      // Also register events for the default model: Some tests don't specify
      // one (on purpose) and we want to control what events are present. Do
      // this by registering them first.
      perfmon::ModelEventManager::RegisterEvents(perfmon::GetDefaultModelName().c_str(), "misc",
                                                 &TestEvents[0], arraysize(TestEvents));
      models_registered_ = true;
    }
  }

  static bool models_registered_;
};

bool SessionSpecTest::models_registered_ = false;

TEST_F(SessionSpecTest, DecodingErrors) {
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

  json = R"({"model_name": 42})";
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

TEST_F(SessionSpecTest, DecodeConfigName) {
  std::string json = R"({"config_name": "test"})";

  SessionSpec result;
  ASSERT_TRUE(DecodeSessionSpec(json, &result));
  EXPECT_EQ("test", result.config_name);
}

TEST_F(SessionSpecTest, DecodeModelName) {
  std::string json = R"({"model_name": "test"})";

  SessionSpec result;
  ASSERT_TRUE(DecodeSessionSpec(json, &result));
  EXPECT_EQ("test", result.model_name);
}

TEST_F(SessionSpecTest, DecodeEvents) {
  std::string json = R"({
  "model_name": "test",
  "events": [
    {
      "group_name": "misc",
      "event_name": "test-event",
      "rate": 42,
      "flags": [ "os", "user", "pc", "timebase" ]
    }
  ]
})";

  SessionSpec result;
  ASSERT_TRUE(DecodeSessionSpec(json, &result));

  const perfmon::EventDetails* details;
  ASSERT_TRUE(result.model_event_manager->LookupEventByName("misc", "test-event", &details));
  perfmon::EventId test_event_id = details->id;

  EXPECT_EQ(result.perfmon_config.GetEventCount(), 1u);
  result.perfmon_config.IterateOverEvents(
      [&test_event_id](const perfmon::Config::EventConfig& event) {
        EXPECT_EQ(event.event, test_event_id);
        EXPECT_EQ(event.rate, 42u);
        EXPECT_EQ(event.flags, (perfmon::Config::kFlagOs | perfmon::Config::kFlagUser |
                                perfmon::Config::kFlagPc | perfmon::Config::kFlagTimebase));
      });
}

TEST_F(SessionSpecTest, DecodeBufferSizeInMb) {
  std::string json = R"({"buffer_size_in_mb": 1})";

  SessionSpec result;
  ASSERT_TRUE(DecodeSessionSpec(json, &result));
  EXPECT_EQ(1u, result.buffer_size_in_mb);
}

TEST_F(SessionSpecTest, DecodeDuration) {
  std::string json = R"({"duration": 42})";

  SessionSpec result;
  ASSERT_TRUE(DecodeSessionSpec(json, &result));
  EXPECT_EQ(zx::sec(42).to_nsecs(), result.duration.to_nsecs());
}

TEST_F(SessionSpecTest, DecodeNumIterations) {
  std::string json = R"({"num_iterations": 99})";

  SessionSpec result;
  ASSERT_TRUE(DecodeSessionSpec(json, &result));
  EXPECT_EQ(99u, result.num_iterations);
}

}  // namespace

}  // namespace cpuperf
