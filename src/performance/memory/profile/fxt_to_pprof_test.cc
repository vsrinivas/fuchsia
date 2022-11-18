
// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/performance/memory/profile/fxt_to_pprof.h"

#include <gtest/gtest.h>
#include <trace-test-utils/fixture.h>

#include "src/performance/memory/profile/test_record_container.h"
#include "src/performance/memory/profile/trace_constants.h"
#include "third_party/protobuf/src/google/protobuf/text_format.h"
#include "third_party/protobuf/src/google/protobuf/util/message_differencer.h"

extern const char* trace_category;
extern "C" {
extern void __scudo_allocate_hook(void* ptr, unsigned int size);
extern void __scudo_deallocate_hook(void* ptr);
}

namespace {

using perfetto::third_party::perftools::profiles::Profile;

const char* trace_category_enabled = "+memory_trace";
const char* trace_category_disabled = "-memory_trace";

struct FixtureCleanup {
  ~FixtureCleanup() { fixture_tear_down(); }
};

// TODO(fxb/114682): Enable this test.
TEST(FxtToPprofTest, DISABLED_Convert) {
  FixtureCleanup cleanup;
  fixture_set_up(kNoAttachToThread, TRACE_BUFFERING_MODE_ONESHOT, /* buffer_size=*/65536);

  fixture_initialize_and_start_tracing();

  trace_category = trace_category_enabled;  // Enable tracing for the fixture.

  char* a = new char[859];
  EXPECT_NE(a, nullptr);  // Prevent allocations to be optimized out.
  delete[] a;
  // TODO(fxb/114682): test residual pairing.
  trace_category = trace_category_disabled;

  fixture_stop_and_terminate_tracing();

  TestRecordContainer record_container;
  ASSERT_TRUE(record_container.ReadFromFixture());
  ASSERT_GT(record_container.records().size(), 0u);

  auto pprof = fxt_to_profile(record_container, trace_category);
  ASSERT_TRUE(pprof.is_ok()) << "Error: " << pprof.error_value();

  EXPECT_EQ(pprof.value().sample_size(), 2) << record_container;
  EXPECT_GT(pprof.value().mapping_size(), 0) << record_container;

  Profile expected;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        sample_type { type: 1 unit: 2 }
        sample_type { type: 3 unit: 4 }
        sample_type { type: 5 unit: 2 }
        sample_type { type: 6 unit: 2 }
        sample_type { type: 7 unit: 4 }
        string_table: ""
        string_table: "new object"
        string_table: "count"
        string_table: "new allocated"
        string_table: "bytes"
        string_table: "free object"
        string_table: "redisual object"
        string_table: "redisual allocated"
        default_sample_type: 1
      )pb",
      &expected));

  google::protobuf::util::MessageDifferencer differ;
  differ.IgnoreField(Profile::GetDescriptor()->FindFieldByName("sample"));
  differ.IgnoreField(Profile::GetDescriptor()->FindFieldByName("mapping"));
  EXPECT_TRUE(differ.Compare(expected, pprof.value())) << "Actual:\n" << pprof->DebugString();
}

TEST(FxtToPprofTest, ConvertEmpty) {
  TestRecordContainer record_container;
  auto pprof = fxt_to_profile(record_container, trace_category);
  ASSERT_FALSE(pprof.is_ok());
}

}  // namespace
