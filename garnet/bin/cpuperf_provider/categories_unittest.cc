// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cpuperf_provider/categories.h"

#include <gtest/gtest.h>

#include <string>
#include <unordered_set>

namespace cpuperf_provider {

namespace {

class CategoryTest : public ::testing::Test {
 public:
  CategoryTest()
      : model_event_manager_(perfmon::ModelEventManager::Create(
            perfmon::GetDefaultModelName())) {}

  perfmon::ModelEventManager* model_event_manager() const {
    return model_event_manager_.get();
  }

 private:
  std::unique_ptr<perfmon::ModelEventManager> model_event_manager_;
};

TEST_F(CategoryTest, Os) {
  const std::unordered_set<std::string> categories{
      "cpu:os",
  };
  TraceConfig::IsCategoryEnabledFunc func = [&categories](const char* name) {
    return categories.find(name) != categories.end();
  };
  auto config = TraceConfig::Create(model_event_manager(), func);
  ASSERT_TRUE(config);

  // Not enabled because there is no data to collect.
  ASSERT_FALSE(config->is_enabled());

  ASSERT_TRUE(config->trace_os());
  ASSERT_FALSE(config->trace_user());
  ASSERT_FALSE(config->trace_pc());
  ASSERT_FALSE(config->trace_last_branch());
  ASSERT_EQ(config->sample_rate(), 0u);
  ASSERT_EQ(config->timebase_event(), perfmon::kEventIdNone);
}

TEST_F(CategoryTest, User) {
  const std::unordered_set<std::string> categories{
      "cpu:user",
  };
  TraceConfig::IsCategoryEnabledFunc func = [&categories](const char* name) {
    return categories.find(name) != categories.end();
  };
  auto config = TraceConfig::Create(model_event_manager(), func);
  ASSERT_TRUE(config);

  // Not enabled because there is no data to collect.
  ASSERT_FALSE(config->is_enabled());

  ASSERT_FALSE(config->trace_os());
  ASSERT_TRUE(config->trace_user());
  ASSERT_FALSE(config->trace_pc());
  ASSERT_FALSE(config->trace_last_branch());
  ASSERT_EQ(config->sample_rate(), 0u);
  ASSERT_EQ(config->timebase_event(), perfmon::kEventIdNone);
}

TEST_F(CategoryTest, NeitherOsNorUser) {
  const std::unordered_set<std::string> categories {
    "cpu:pc",
#if defined(__x86_64__)
        "cpu:fixed:instructions_retired",
#elif defined(__aarch64__)
        "cpu:fixed:cycle_counter",
#endif
        "cpu:sample:1000",
  };
  TraceConfig::IsCategoryEnabledFunc func = [&categories](const char* name) {
    return categories.find(name) != categories.end();
  };
  auto config = TraceConfig::Create(model_event_manager(), func);
  ASSERT_TRUE(config);
  ASSERT_TRUE(config->is_enabled());
  // If neither os nor user are specified, then both are enabled.
  ASSERT_TRUE(config->trace_os());
  ASSERT_TRUE(config->trace_user());
  ASSERT_TRUE(config->trace_pc());
  ASSERT_EQ(config->sample_rate(), 1000u);
  ASSERT_EQ(config->timebase_event(), perfmon::kEventIdNone);
}

TEST_F(CategoryTest, Timebase) {
  const std::unordered_set<std::string> categories {
    "cpu:pc",
#if defined(__x86_64__)
        "cpu:timebase:fixed:instructions_retired",
        "cpu:fixed:instructions_retired",
#elif defined(__aarch64__)
        "cpu:timebase:fixed:cycle_counter", "cpu:fixed:cycle_counter",
#endif
        "cpu:sample:1000",
  };
  TraceConfig::IsCategoryEnabledFunc func = [&categories](const char* name) {
    return categories.find(name) != categories.end();
  };
  auto config = TraceConfig::Create(model_event_manager(), func);
  ASSERT_TRUE(config);
  ASSERT_TRUE(config->is_enabled());
  // If neither os nor user are specified, then both are enabled.
  ASSERT_TRUE(config->trace_os());
  ASSERT_TRUE(config->trace_user());
  ASSERT_TRUE(config->trace_pc());
  ASSERT_EQ(config->sample_rate(), 1000u);
  ASSERT_NE(config->timebase_event(), perfmon::kEventIdNone);
}

}  // namespace

}  // namespace cpuperf_provider
