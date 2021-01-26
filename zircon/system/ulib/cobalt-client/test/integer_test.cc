// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sync/completion.h>
#include <stdint.h>

#include <array>
#include <cstdint>
#include <mutex>
#include <thread>

#include <cobalt-client/cpp/counter_internal.h>
#include <cobalt-client/cpp/in_memory_logger.h>
#include <cobalt-client/cpp/integer.h>
#include <cobalt-client/cpp/metric_options.h>
#include <zxtest/zxtest.h>

namespace cobalt_client {
namespace {

using internal::RemoteInteger;

// Default |MetricOption| values.
constexpr uint64_t kMetricId = 1;
constexpr std::string_view kComponentName = "TestInteger";
constexpr std::array<uint32_t, MetricOptions::kMaxEventCodes> kEventCodes = {0, 1, 2, 3, 4};

// Returns a set of options with the default values.
MetricOptions MakeMetricOptions() {
  MetricOptions options = {};
  options.metric_id = kMetricId;
  options.component = kComponentName;
  options.event_codes = kEventCodes;
  return options;
}

RemoteInteger MakeRemoteInteger() { return RemoteInteger(MakeMetricOptions()); }

TEST(RemoteIntegerTest, FlushSetsIntegerToZeroAndReturnsTrueIfLogSucceeds) {
  constexpr int64_t kValue = 25;
  InMemoryLogger logger;
  RemoteInteger integer = MakeRemoteInteger();
  logger.fail_logging(false);

  ASSERT_EQ(0, integer.Load());
  integer.Exchange(kValue);

  ASSERT_TRUE(integer.Flush(&logger));
  ASSERT_NE(logger.counters().end(), logger.counters().find(integer.metric_options()));

  EXPECT_EQ(0, integer.Load());
  EXPECT_EQ(kValue, logger.counters().at(integer.metric_options()));
}

TEST(RemoteIntegerTest, FlushSetsIntegerToZeroAndReturnsFalseIfLogSucceeds) {
  constexpr int64_t kValue = 25;
  InMemoryLogger logger;
  RemoteInteger integer = MakeRemoteInteger();
  logger.fail_logging(true);

  ASSERT_EQ(0, integer.Load());
  integer.Increment(kValue);

  ASSERT_FALSE(integer.Flush(&logger));
  ASSERT_EQ(logger.counters().end(), logger.counters().find(integer.metric_options()));

  // Still resets itself.
  EXPECT_EQ(0, integer.Load());
}

TEST(RemoteIntegerTest, UndoFlushSetsIntegerToPreviousValue) {
  constexpr int64_t kValue = 25;
  InMemoryLogger logger;
  RemoteInteger integer = MakeRemoteInteger();
  logger.fail_logging(true);

  ASSERT_EQ(0, integer.Load());
  integer.Increment(kValue);

  ASSERT_FALSE(integer.Flush(&logger));
  integer.UndoFlush();

  EXPECT_EQ(kValue, integer.Load());
}

TEST(IntegerTest, ConstructFromOptionsIsOk) {
  ASSERT_NO_DEATH([] { [[maybe_unused]] Integer integer(MakeMetricOptions()); });
}

TEST(IntegerTest, ConstructFromOptionsWithCollectorIsOk) {
  ASSERT_NO_DEATH([] {
    std::unique_ptr<InMemoryLogger> logger = std::make_unique<InMemoryLogger>();
    Collector collector(std::move(logger));
    [[maybe_unused]] Integer integer(MakeMetricOptions(), &collector);
  });
}

TEST(IntegerTest, InitializeWithOptionsAndCollectorIsOk) {
  ASSERT_NO_DEATH([]() {
    std::unique_ptr<InMemoryLogger> logger = std::make_unique<InMemoryLogger>();
    Collector collector(std::move(logger));
    Integer integer;
    integer.Initialize(MakeMetricOptions(), &collector);
  });
}

TEST(IntegerTest, InitilizeAlreadyInitializedIntegerIsAssertionError) {
  std::unique_ptr<InMemoryLogger> logger = std::make_unique<InMemoryLogger>();
  Collector collector(std::move(logger));
  Integer integer(MakeMetricOptions(), &collector);
  ASSERT_DEATH([&]() { integer.Initialize(MakeMetricOptions(), &collector); });
}

TEST(IntegerTest, SetSetsValue) {
  Integer integer(MakeMetricOptions());

  integer.Set(5);

  EXPECT_EQ(5, integer.Get());
}

}  // namespace
}  // namespace cobalt_client
