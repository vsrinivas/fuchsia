
// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/trace/event.h>

#include <latch>
#include <thread>

#include <gtest/gtest.h>
#include <trace-test-utils/fixture.h>

namespace {

// This test is a reproduction for fxb/114682 issue.
// To be moved to trace-tes-utils when the issue is resolved.
TEST(TraceFixtureTest, DISABLED_Background) {
  for (int repeat_test = 0; repeat_test < 100; repeat_test++) {
    printf("Iteration %d\n", repeat_test);

    fixture_set_up(kNoAttachToThread, TRACE_BUFFERING_MODE_ONESHOT, 65536);
    // Trace a single record as soon as the trace system is enabled.
    std::latch background_is_running{1};
    std::thread background_thread([&background_is_running]() {
      background_is_running.count_down();
      while (!TRACE_ENABLED()) {
      }
      TRACE_BLOB_EVENT("+test_category", "background_event", "Sometimes I get lost", 3);
    });
    background_is_running.wait();
    fixture_initialize_and_start_tracing();
    background_thread.join();
    fixture_stop_and_terminate_tracing();

    // Find the record traced by the thread there should be only one.
    fbl::Vector<trace::Record> records;
    ASSERT_TRUE(fixture_read_records(&records));
    int count = 0;
    for (auto& r : records) {
      if (r.type() != trace::RecordType::kLargeRecord)
        continue;
      auto& l = r.GetLargeRecord();
      if (l.type() != trace::LargeRecordType::kBlob)
        continue;
      auto& b = std::get<trace::LargeRecordData::BlobEvent>(l.GetBlob());
      count += b.category == "+test_category";
    }

    ASSERT_EQ(count, 1) << "There should be exactly one record";
    fixture_tear_down();
  }
}

}  // namespace
