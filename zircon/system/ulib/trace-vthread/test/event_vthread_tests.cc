// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <stddef.h>

#include <array>
#include <memory>

#include <trace-engine/context.h>
#include <trace-engine/handler.h>
#include <trace-engine/types.h>
#include <trace-provider/handler.h>
#include <trace-test-utils/compare_records.h>
#include <trace-test-utils/read_records.h>
#include <unittest/unittest.h>

#include "trace-vthread/event_vthread.h"

// Helper macros for writing tests.
#define STR_ARGS1 "k1", TA_STRING("v1")
#define STR_ARGS4 \
  "k1", TA_STRING("v1"), "k2", TA_STRING("v2"), "k3", TA_STRING("v3"), "k4", TA_STRING("v4")

class TraceFixture : private trace::TraceHandler {
 public:
  static constexpr trace_buffering_mode_t kBufferingMode = TRACE_BUFFERING_MODE_ONESHOT;

  static constexpr size_t kBufferSize = 1024 * 1024;

  TraceFixture() { buffer_.reset(new std::array<uint8_t, kBufferSize>); }

  bool StartTracing() {
    zx_status_t init_status = trace_engine_initialize(loop_.dispatcher(), this, kBufferingMode,
                                                      buffer_->data(), buffer_->size());
    zx_status_t start_status = trace_engine_start(TRACE_START_CLEAR_ENTIRE_BUFFER);
    return init_status == ZX_OK && start_status == ZX_OK;
  }

  bool StopTracing() {
    trace_engine_terminate();
    loop_.RunUntilIdle();
    return true;
  }

  bool CompareBuffer(const char* expected) {
    BEGIN_HELPER;
    fbl::Vector<trace::Record> records;
    ASSERT_TRUE(trace_testing::ReadRecords(buffer_->data(), buffer_->size(), &records));
    ASSERT_TRUE(trace_testing::CompareBuffer(records, expected));
    END_HELPER;
  }

 private:
  async::Loop loop_{&kAsyncLoopConfigAttachToCurrentThread};

  std::unique_ptr<std::array<uint8_t, kBufferSize>> buffer_;
};

bool TestVthreadDurationBegin() {
  TraceFixture fixture;

  BEGIN_TEST;

  ASSERT_TRUE(fixture.StartTracing());

  TRACE_VTHREAD_DURATION_BEGIN("+enabled", "name", "virtual-thread", 1u, zx_ticks_get());
  TRACE_VTHREAD_DURATION_BEGIN("+enabled", "name", "virtual-thread", 1u, zx_ticks_get(), STR_ARGS1);
  TRACE_VTHREAD_DURATION_BEGIN("+enabled", "name", "virtual-thread", 1u, zx_ticks_get(), STR_ARGS4);

  ASSERT_TRUE(fixture.StopTracing());

  ASSERT_TRUE(
      fixture.CompareBuffer("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"virtual-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 3, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {})\n\
String(index: 4, \"k1\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {k1: string(\"v1\")})\n\
String(index: 5, \"k2\")\n\
String(index: 6, \"k3\")\n\
String(index: 7, \"k4\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationBegin, {k1: string(\"v1\"), k2: string(\"v2\"), k3: string(\"v3\"), k4: string(\"v4\")})\n\
"));

  END_TEST;
}

bool TestVthreadDurationEnd() {
  TraceFixture fixture;

  BEGIN_TEST;

  ASSERT_TRUE(fixture.StartTracing());

  TRACE_VTHREAD_DURATION_END("+enabled", "name", "virtual-thread", 1u, zx_ticks_get());
  TRACE_VTHREAD_DURATION_END("+enabled", "name", "virtual-thread", 1u, zx_ticks_get(), STR_ARGS1);
  TRACE_VTHREAD_DURATION_END("+enabled", "name", "virtual-thread", 1u, zx_ticks_get(), STR_ARGS4);

  ASSERT_TRUE(fixture.StopTracing());

  ASSERT_TRUE(
      fixture.CompareBuffer("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"virtual-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 3, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationEnd, {})\n\
String(index: 4, \"k1\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationEnd, {k1: string(\"v1\")})\n\
String(index: 5, \"k2\")\n\
String(index: 6, \"k3\")\n\
String(index: 7, \"k4\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", DurationEnd, {k1: string(\"v1\"), k2: string(\"v2\"), k3: string(\"v3\"), k4: string(\"v4\")})\n\
"));

  END_TEST;
}

bool TestVthreadFlowBegin() {
  TraceFixture fixture;

  BEGIN_TEST;

  ASSERT_TRUE(fixture.StartTracing());

  TRACE_VTHREAD_FLOW_BEGIN("+enabled", "name", "virtual-thread", 1u, 2u, zx_ticks_get());
  TRACE_VTHREAD_FLOW_BEGIN("+enabled", "name", "virtual-thread", 1u, 2u, zx_ticks_get(), STR_ARGS1);
  TRACE_VTHREAD_FLOW_BEGIN("+enabled", "name", "virtual-thread", 1u, 2u, zx_ticks_get(), STR_ARGS4);

  ASSERT_TRUE(fixture.StopTracing());

  ASSERT_TRUE(
      fixture.CompareBuffer("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"virtual-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 3, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", FlowBegin(id: 2), {})\n\
String(index: 4, \"k1\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", FlowBegin(id: 2), {k1: string(\"v1\")})\n\
String(index: 5, \"k2\")\n\
String(index: 6, \"k3\")\n\
String(index: 7, \"k4\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", FlowBegin(id: 2), {k1: string(\"v1\"), k2: string(\"v2\"), k3: string(\"v3\"), k4: string(\"v4\")})\n\
"));

  END_TEST;
}

bool TestVthreadFlowStep() {
  TraceFixture fixture;

  BEGIN_TEST;

  ASSERT_TRUE(fixture.StartTracing());

  TRACE_VTHREAD_FLOW_STEP("+enabled", "name", "virtual-thread", 1u, 2u, zx_ticks_get());
  TRACE_VTHREAD_FLOW_STEP("+enabled", "name", "virtual-thread", 1u, 2u, zx_ticks_get(), STR_ARGS1);
  TRACE_VTHREAD_FLOW_STEP("+enabled", "name", "virtual-thread", 1u, 2u, zx_ticks_get(), STR_ARGS4);

  ASSERT_TRUE(fixture.StopTracing());

  ASSERT_TRUE(
      fixture.CompareBuffer("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"virtual-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 3, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", FlowStep(id: 2), {})\n\
String(index: 4, \"k1\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", FlowStep(id: 2), {k1: string(\"v1\")})\n\
String(index: 5, \"k2\")\n\
String(index: 6, \"k3\")\n\
String(index: 7, \"k4\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", FlowStep(id: 2), {k1: string(\"v1\"), k2: string(\"v2\"), k3: string(\"v3\"), k4: string(\"v4\")})\n\
"));

  END_TEST;
}

bool TestVthreadFlowEnd() {
  TraceFixture fixture;

  BEGIN_TEST;

  ASSERT_TRUE(fixture.StartTracing());

  TRACE_VTHREAD_FLOW_END("+enabled", "name", "virtual-thread", 1u, 2u, zx_ticks_get());
  TRACE_VTHREAD_FLOW_END("+enabled", "name", "virtual-thread", 1u, 2u, zx_ticks_get(), STR_ARGS1);
  TRACE_VTHREAD_FLOW_END("+enabled", "name", "virtual-thread", 1u, 2u, zx_ticks_get(), STR_ARGS4);

  ASSERT_TRUE(fixture.StopTracing());

  ASSERT_TRUE(
      fixture.CompareBuffer("\
String(index: 1, \"+enabled\")\n\
String(index: 2, \"process\")\n\
KernelObject(koid: <>, type: thread, name: \"virtual-thread\", {process: koid(<>)})\n\
Thread(index: 1, <>)\n\
String(index: 3, \"name\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", FlowEnd(id: 2), {})\n\
String(index: 4, \"k1\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", FlowEnd(id: 2), {k1: string(\"v1\")})\n\
String(index: 5, \"k2\")\n\
String(index: 6, \"k3\")\n\
String(index: 7, \"k4\")\n\
Event(ts: <>, pt: <>, category: \"+enabled\", name: \"name\", FlowEnd(id: 2), {k1: string(\"v1\"), k2: string(\"v2\"), k3: string(\"v3\"), k4: string(\"v4\")})\n\
"));

  END_TEST;
}

BEGIN_TEST_CASE(event_thread_tests)
RUN_TEST(TestVthreadDurationBegin)
RUN_TEST(TestVthreadDurationEnd)
RUN_TEST(TestVthreadFlowBegin)
RUN_TEST(TestVthreadFlowStep)
RUN_TEST(TestVthreadFlowEnd)
END_TEST_CASE(event_thread_tests)
