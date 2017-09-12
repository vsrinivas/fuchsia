// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/process.h>
#include <mx/eventpair.h>
#include <mx/vmo.h>

#include "apps/tracing/lib/trace/cevent.h"
#include "apps/tracing/lib/trace/event.h"
#include "apps/tracing/lib/trace/tests/cevent_unittest.h"
#include "gtest/gtest.h"
#include "lib/fxl/logging.h"
#include "lib/fsl/tasks/message_loop.h"

namespace tracing {
namespace writer {
namespace {

struct CEventTest : public ::testing::Test {
  CEventTest() {
    mx::vmo buffer;
    mx::eventpair fence;
    assert(MX_OK == mx::vmo::create(100000, 0u, &buffer));
    assert(MX_OK == mx::eventpair::create(0u, &fence, &control_));
    StartTracing(std::move(buffer), std::move(fence), {"cat"},
                 [this](TraceDisposition disposition) { loop_.QuitNow(); });
  }

  ~CEventTest() {
    StopTracing();
    loop_.Run();
  }

 private:
  fsl::MessageLoop loop_;
  mx::eventpair control_;
};

TEST_F(CEventTest, Enabled) {
  EXPECT_EQ(cevent_test_enabled(), true);
}

TEST_F(CEventTest, CategoryEnabled) {
  EXPECT_EQ(cevent_test_category_enabled(), true);
}

TEST_F(CEventTest, TraceNonce) {
  EXPECT_EQ(cevent_test_trace_nonce(), true);
}

TEST_F(CEventTest, Instant) {
  EXPECT_EQ(cevent_test_instant(), true);
}

TEST_F(CEventTest, Counter) {
  EXPECT_EQ(cevent_test_counter(), true);
}

TEST_F(CEventTest, Duration) {
  EXPECT_EQ(cevent_test_duration(), true);
}

TEST_F(CEventTest, DurationBegin) {
  EXPECT_EQ(cevent_test_duration_begin(), true);
}

TEST_F(CEventTest, DurationEnd) {
  EXPECT_EQ(cevent_test_duration_end(), true);
}

TEST_F(CEventTest, AsyncBegin) {
  EXPECT_EQ(cevent_test_async_begin(), true);
}

TEST_F(CEventTest, AsyncInstant) {
  EXPECT_EQ(cevent_test_async_instant(), true);
}

TEST_F(CEventTest, AsyncEnd) {
  EXPECT_EQ(cevent_test_async_end(), true);
}

TEST_F(CEventTest, FlowBegin) {
  EXPECT_EQ(cevent_test_flow_begin(), true);
}

TEST_F(CEventTest, FlowStep) {
  EXPECT_EQ(cevent_test_flow_step(), true);
}

TEST_F(CEventTest, FlowEnd) {
  EXPECT_EQ(cevent_test_flow_end(), true);
}

TEST_F(CEventTest, Handle) {
  EXPECT_EQ(cevent_test_handle(), true);
}

TEST_F(CEventTest, NullArguments) {
  EXPECT_EQ(cevent_test_null_arguments(), true);
}

TEST_F(CEventTest, IntegralArguments) {
  EXPECT_EQ(cevent_test_integral_arguments(), true);
}

TEST_F(CEventTest, EnumArguments) {
  EXPECT_EQ(cevent_test_enum_arguments(), true);
}

TEST_F(CEventTest, FloatArguments) {
  EXPECT_EQ(cevent_test_float_arguments(), true);
}

TEST_F(CEventTest, StringArguments) {
  EXPECT_EQ(cevent_test_string_arguments(), true);
}

TEST_F(CEventTest, PointerArguments) {
  EXPECT_EQ(cevent_test_pointer_arguments(), true);
}

TEST_F(CEventTest, KoidArguments) {
  EXPECT_EQ(cevent_test_koid_arguments(), true);
}

}  // namespace
}  // namespace writer
}  // namespace tracing

void ctrace_stop_tracing() {
  ::tracing::writer::StopTracing();
}
