// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/trace/event.h>
#include <lib/trace/event_args.h>
#include <zircon/syscalls.h>

#include <trace-test-utils/fixture.h>
#include <zxtest/zxtest.h>

#include <fbl/string.h>
#include <fbl/string_piece.h>

#include "fixture_macros.h"
#include "lib.h"

#define TEST_SUITE rust_test

TEST(TEST_SUITE, test_trace_enabled) {
  BEGIN_TRACE_TEST;

  EXPECT_FALSE(rs_test_trace_enabled());

  fixture_initialize_and_start_tracing();

  EXPECT_TRUE(rs_test_trace_enabled());
}

TEST(TEST_SUITE, test_category_enabled) {
  BEGIN_TRACE_TEST;

  fixture_initialize_and_start_tracing();

  EXPECT_FALSE(rs_test_category_disabled());
  EXPECT_TRUE(rs_test_category_enabled());
}

TEST(TEST_SUITE, test_counter) {
  BEGIN_TRACE_TEST;

  fixture_initialize_and_start_tracing();

  rs_test_counter_macro();

  ASSERT_RECORDS(
    "String(index: 1, \"+enabled\")\n"
    "String(index: 2, \"process\")\n"
    "KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n"
    "Thread(index: 1, <>)\n"
    "String(index: 3, \"name\")\n"
    "Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", Counter(id: 42), {arg: int32(10)})\n"
  );
}

TEST(TEST_SUITE, test_instant) {
  BEGIN_TRACE_TEST;

  fixture_initialize_and_start_tracing();

  rs_test_instant_macro();

  ASSERT_RECORDS(
    "String(index: 1, \"+enabled\")\n"
    "String(index: 2, \"process\")\n"
    "KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n"
    "Thread(index: 1, <>)\n"
    "String(index: 3, \"name\")\n"
    "Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", Instant(scope: process), {arg: int32(10)})\n"
  );
}

TEST(TEST_SUITE, test_duration) {
  BEGIN_TRACE_TEST;

  fixture_initialize_and_start_tracing();

  rs_test_duration_macro();

  ASSERT_RECORDS(
    "String(index: 1, \"+enabled\")\n"
    "String(index: 2, \"process\")\n"
    "KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n"
    "Thread(index: 1, <>)\n"
    "String(index: 3, \"name\")\n"
    "Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationComplete(end_ts: <>), {x: int32(5), y: int32(10)})\n"
  );
}

TEST(TEST_SUITE, test_scoped_duration) {
  BEGIN_TRACE_TEST;

  fixture_initialize_and_start_tracing();

  rs_test_duration_macro_with_scope();

  ASSERT_RECORDS(
    "String(index: 1, \"+enabled\")\n"
    "String(index: 2, \"process\")\n"
    "KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n"
    "Thread(index: 1, <>)\n"
    "String(index: 3, \"name\")\n"
    "Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", Instant(scope: process), {arg: int32(10)})\n"
    "Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationComplete(end_ts: <>), {x: int32(5), y: int32(10)})\n"
  );
}

TEST(TEST_SUITE, test_duration_granular) {
  BEGIN_TRACE_TEST;

  fixture_initialize_and_start_tracing();

  rs_test_duration_begin_end_macros();

  ASSERT_RECORDS(
    "String(index: 1, \"+enabled\")\n"
    "String(index: 2, \"process\")\n"
    "KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n"
    "Thread(index: 1, <>)\n"
    "String(index: 3, \"name\")\n"
    "Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {x: int32(5)})\n"
    "Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", Instant(scope: process), {arg: int32(10)})\n"
    "Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationEnd, {y: string(\"foo\")})\n"
  );
}

TEST(TEST_SUITE, test_blob) {
  BEGIN_TRACE_TEST;

  fixture_initialize_and_start_tracing();

  rs_test_blob_macro();

  ASSERT_RECORDS(
    "String(index: 1, \"+enabled\")\n"
    "String(index: 2, \"process\")\n"
    "KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n"
    "Thread(index: 1, <>)\n"
    "String(index: 3, \"name\")\n"
    "LargeRecord(Blob(format: blob_event, category: \"+enabled\", name: \"name\", ts: <>, pt: <>, {x: int32(5)}, size: 13, preview: <62 6c 6f 62 20 63 6f 6e 74 65 6e 74 73>))\n"
  );
}

TEST(TEST_SUITE, test_flow) {
  BEGIN_TRACE_TEST;

  fixture_initialize_and_start_tracing();

  rs_test_flow_begin_step_end_macros();

  ASSERT_RECORDS(
    "String(index: 1, \"+enabled\")\n"
    "String(index: 2, \"process\")\n"
    "KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n"
    "Thread(index: 1, <>)\n"
    "String(index: 3, \"name\")\n"
    "Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", FlowBegin(id: 123), {x: int32(5)})\n"
    "String(index: 4, \"step\")\n"
    "Event(ts: <>, pt: <>, category: \"+enabled\", name: \"step\", FlowStep(id: 123), {z: int32(42)})\n"
    "Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", FlowEnd(id: 123), {y: string(\"foo\")})\n"
  );
}

TEST(TEST_SUITE, test_arglimit) {
  BEGIN_TRACE_TEST;

  fixture_initialize_and_start_tracing();

  rs_test_arglimit();

  ASSERT_RECORDS(
    "String(index: 1, \"+enabled\")\n"
    "String(index: 2, \"process\")\n"
    "KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n"
    "Thread(index: 1, <>)\n"
    "String(index: 3, \"name\")\n"
    "Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationComplete(end_ts: <>), {1: int32(1), 2: int32(2), 3: int32(3), 4: int32(4), 5: int32(5), 6: int32(6), 7: int32(7), 8: int32(8), 9: int32(9), 10: int32(10), 11: int32(11), 12: int32(12), 13: int32(13), 14: int32(14), 15: int32(15)})\n"
  );
}
