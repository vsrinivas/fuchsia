// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "garnet/lib/perfmon/config.h"

namespace perfmon {
namespace {

// We use fake events here as these values are just passed to the driver,
// and this let's us be architecture-independent.
constexpr EventId kEventOne = MakeEventId(kGroupMisc, 1);
constexpr EventId kEventTwo = MakeEventId(kGroupMisc, 2);

TEST(Config, Events) {
  Config config;

  EXPECT_EQ(config.GetEventCount(), 0u);

  EXPECT_EQ(config.AddEvent(kEventOne, 0, 0),
            Config::Status::OK);
  EXPECT_EQ(config.GetEventCount(), 1u);

  EXPECT_EQ(config.AddEvent(kEventTwo, 1000, Config::kFlagOs),
            Config::Status::OK);
  EXPECT_EQ(config.GetEventCount(), 2u);

  std::vector<Config::EventConfig> events;
  config.IterateOverEvents([&events](const Config::EventConfig& event) {
    events.push_back(event);
  });

  EXPECT_EQ(events.size(), 2u);
  for (const auto& event : events) {
    switch (event.event) {
    case kEventOne:
      EXPECT_EQ(event.rate, 0u);
      EXPECT_EQ(event.flags, 0u);
      break;
    case kEventTwo:
      EXPECT_EQ(event.rate, 1000u);
      EXPECT_EQ(event.flags, Config::kFlagOs);
      break;
    }
  }
}

TEST(Config, SampleMode) {
  Config config;

  EXPECT_EQ(config.AddEvent(kEventOne, 1000, Config::kFlagOs),
            Config::Status::OK);

  EXPECT_EQ(config.GetMode(), CollectionMode::kSample);
}

TEST(Config, TallyMode) {
  Config config;

  EXPECT_EQ(config.AddEvent(kEventTwo, 0, Config::kFlagOs),
            Config::Status::OK);

  EXPECT_EQ(config.GetMode(), CollectionMode::kTally);
}

TEST(Config, Reset) {
  Config config;

  EXPECT_EQ(config.AddEvent(kEventOne, 10, Config::kFlagOs),
            Config::Status::OK);
  EXPECT_EQ(config.AddEvent(kEventTwo, 1000, Config::kFlagUser),
            Config::Status::OK);
  EXPECT_EQ(config.GetEventCount(), 2u);

  config.Reset();
  EXPECT_EQ(config.GetEventCount(), 0u);
}

TEST(Config, ToString) {
  Config config;

  EXPECT_EQ(config.AddEvent(kEventOne, 10, Config::kFlagOs),
            Config::Status::OK);

  EXPECT_EQ(config.ToString(), "event 0x2001, rate 10, flags 0x1");
}

}  // namespace
}  // namespace perfmon
