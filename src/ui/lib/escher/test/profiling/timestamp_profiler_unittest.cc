// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/profiling/timestamp_profiler.h"

#include <gtest/gtest.h>

namespace escher {
TEST(Trace, EmptyInput) {
  std::vector<TimestampProfiler::Result> ts;

  std::vector<TimestampProfiler::TraceEvent> trace_events =
      TimestampProfiler::ProcessTraceEvents(ts);

  EXPECT_EQ(trace_events.size(), 0u);
}

TEST(Trace, TooSmallInput) {
  std::vector<TimestampProfiler::Result> ts;
  ts.push_back({});

  std::vector<TimestampProfiler::TraceEvent> trace_events =
      TimestampProfiler::ProcessTraceEvents(ts);

  EXPECT_EQ(trace_events.size(), 0u);
}

TEST(Trace, OneSingularEvent) {
  std::vector<TimestampProfiler::Result> ts;
  const char* name = "event";

  ts.push_back({1000, 0, 0, "start"});
  ts.push_back({2000, 1, 1, name});
  ts.push_back({3000, 2, 1, "end"});

  std::vector<TimestampProfiler::TraceEvent> trace_events =
      TimestampProfiler::ProcessTraceEvents(ts);

  // Check the number of non-overlapping events.
  EXPECT_EQ(trace_events.size(), 1u);

  // Check the number of each event's concurrent events.
  EXPECT_EQ(trace_events[0].names.size(), 1u);

  // Check pointer equality for each event's name.
  EXPECT_EQ(trace_events[0].names[0], name);
}

TEST(Trace, MultipleSingularEvents) {
  std::vector<TimestampProfiler::Result> ts;
  const char* name1 = "event1";
  const char* name2 = "event2";

  ts.push_back({1000, 0, 0, "start"});
  ts.push_back({2000, 1, 1, name1});
  ts.push_back({3000, 2, 1, name2});
  ts.push_back({4000, 3, 1, "end"});

  std::vector<TimestampProfiler::TraceEvent> trace_events =
      TimestampProfiler::ProcessTraceEvents(ts);

  // Check the number of non-overlapping events.
  EXPECT_EQ(trace_events.size(), 2u);

  // Check the number of each event's concurrent events.
  EXPECT_EQ(trace_events[0].names.size(), 1u);
  EXPECT_EQ(trace_events[1].names.size(), 1u);

  // Check pointer equality for each event's name.
  EXPECT_EQ(trace_events[0].names[0], name1);
  EXPECT_EQ(trace_events[1].names[0], name2);
}

TEST(Trace, OneConcurrentEvent) {
  std::vector<TimestampProfiler::Result> ts;
  const char* name1a = "event1a";
  const char* name1b = "event1b";

  ts.push_back({1000, 0, 0, "start"});
  ts.push_back({2000, 1, 1, name1a});
  ts.push_back({2000, 1, 0, name1b});
  ts.push_back({3000, 2, 1, "end"});

  std::vector<TimestampProfiler::TraceEvent> trace_events =
      TimestampProfiler::ProcessTraceEvents(ts);

  // Check the number of non-overlapping events.
  EXPECT_EQ(trace_events.size(), 1u);

  // Check the number of each event's concurrent events.
  EXPECT_EQ(trace_events[0].names.size(), 2u);

  // Check pointer equality for each event's name.
  EXPECT_EQ(trace_events[0].names[0], name1a);
  EXPECT_EQ(trace_events[0].names[1], name1b);
}

TEST(Trace, MultipleConcurrentEvents) {
  std::vector<TimestampProfiler::Result> ts;
  const char* name1a = "event1a";
  const char* name1b = "event1b";
  const char* name2a = "event2a";
  const char* name2b = "event2b";
  const char* name2c = "event2c";
  const char* name3a = "event3a";

  ts.push_back({1000, 0, 0, "start"});
  ts.push_back({2000, 1, 1, name1a});
  ts.push_back({2000, 1, 0, name1b});
  ts.push_back({3000, 2, 1, name2a});
  ts.push_back({3000, 2, 0, name2b});
  ts.push_back({3000, 2, 0, name2c});
  ts.push_back({4000, 3, 1, name3a});
  ts.push_back({5000, 4, 1, "end"});

  std::vector<TimestampProfiler::TraceEvent> trace_events =
      TimestampProfiler::ProcessTraceEvents(ts);

  // Check the number of non-overlapping events.
  EXPECT_EQ(trace_events.size(), 3u);

  // Check the number of each event's concurrent events.
  EXPECT_EQ(trace_events[0].names.size(), 2u);
  EXPECT_EQ(trace_events[1].names.size(), 3u);
  EXPECT_EQ(trace_events[2].names.size(), 1u);

  // Check pointer equality for each event's name.
  EXPECT_EQ(trace_events[0].names[0], name1a);
  EXPECT_EQ(trace_events[0].names[1], name1b);
  EXPECT_EQ(trace_events[1].names[0], name2a);
  EXPECT_EQ(trace_events[1].names[1], name2b);
  EXPECT_EQ(trace_events[1].names[2], name2c);
  EXPECT_EQ(trace_events[2].names[0], name3a);
}

}  // namespace escher
