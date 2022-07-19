// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "lib/zircon-internal/device/cpu-trace/perf-mon.h"
#include "src/performance/lib/perfmon/config.h"
#include "src/performance/lib/perfmon/controller.h"
#include "src/performance/lib/perfmon/events.h"
#include "src/performance/lib/perfmon/reader.h"
#include "src/performance/lib/perfmon/records.h"
#include "zircon/errors.h"

TEST(Perfmon, BasicCount) {
  // We set up perfmon in counting mode, start, stop, then check for records.
  perfmon::Config config;
  const uint32_t buffer_size_in_pages = 1000;  // 4 MB

#if defined(__aarch64__)
  // Arm64: cpu cycles
  perfmon::EventId event = perfmon::MakeEventId(perfmon::kGroupArch, 17);
#elif defined(__x86_64)
  // Intel: unhalted core cycles
  perfmon::EventId event = perfmon::MakeEventId(perfmon::kGroupArch, 0);
#else
#error Unsupported architecture
#endif

  ASSERT_EQ(perfmon::Config::Status::OK,
            config.AddEvent(event, 0, perfmon::Config::kFlagUser | perfmon::Config::kFlagOs));

  // We need an actual pmu to run this. If we don't have one, instead check
  // that we fail to start.
  auto controller = perfmon::Controller::Create(buffer_size_in_pages, config);
  if (!perfmon::Controller::IsSupported()) {
    EXPECT_TRUE(controller.is_error());
    return;
  }
  ASSERT_TRUE(controller.is_ok());
  ASSERT_TRUE(controller->Start().is_ok());
  ASSERT_TRUE(controller->Stop().is_ok());

  // We should expect to see a timestamp record followed by a count record with
  // the event we configured.
  auto reader = controller->GetReader();
  ASSERT_GT(reader->num_traces(), uint32_t{0});
  perfmon::SampleRecord record;
  uint32_t trace_num;
  reader->ReadNextRecord(&trace_num, &record);
  EXPECT_EQ(record.type(), perfmon::kRecordTypeTime);
  reader->ReadNextRecord(&trace_num, &record);
  EXPECT_EQ(record.type(), perfmon::kRecordTypeCount);
  const perfmon::CountRecord* count_record = record.count;
  EXPECT_EQ(count_record->header.event, event);

  // Perfmon records are 4 byte aligned. Attempting to directly use EXPECT_GT
  // on count_record->count takes it by reference and can result in an
  // unaligned reference as uint64_t's need to be 8 byte aligned.
  uint64_t count = count_record->count;
  EXPECT_GT(count, uint64_t{0});
}
