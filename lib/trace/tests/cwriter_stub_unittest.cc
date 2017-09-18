// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/trace/writer.h"
#include "garnet/lib/trace/cevent.h"

#include "gtest/gtest.h"

namespace tracing {
namespace writer {
namespace {

TEST(CWriterStubTest, AllFunctionsDoNothing) {
  EXPECT_FALSE(StartTracing(zx::vmo(), zx::eventpair(), {},
                            [](TraceDisposition disposition) {}));
  StopTracing();
  EXPECT_FALSE(ctrace_is_enabled());
  EXPECT_FALSE(ctrace_category_is_enabled("cat"));
  EXPECT_TRUE(GetEnabledCategories().empty());
  EXPECT_EQ(TraceState::kFinished, GetTraceState());
  TraceHandlerKey handler_key = AddTraceHandler([](TraceState state) {});
  RemoveTraceHandler(handler_key);

  auto writer = ctrace_writer_acquire();
  EXPECT_FALSE(writer);
  if (writer) {
    // These methods will not be reached but their symbols must be present.

    ctrace_stringref_t stringref;
    ctrace_register_string(writer, "", &stringref);
    ctrace_register_category_string(writer, "", false, &stringref);

    ctrace_threadref_t threadref;
    ctrace_register_current_thread(writer, &threadref);

    // Stubs for the internal functions (used by the TRACE_* macros).
    ctrace_internal_write_instant_event_record(NULL, NULL, NULL, CTRACE_SCOPE_THREAD, NULL);
    ctrace_internal_write_counter_event_record(NULL, NULL, NULL, 0u, NULL);
    ctrace_internal_write_duration_begin_event_record(NULL, NULL, NULL, NULL);
    ctrace_internal_write_duration_end_event_record(NULL, NULL, NULL, NULL);
    ctrace_internal_write_async_begin_event_record(NULL, NULL, NULL, 0u, NULL);
    ctrace_internal_write_async_instant_event_record(NULL, NULL, NULL, 0u, NULL);
    ctrace_internal_write_async_end_event_record(NULL, NULL, NULL, 0u, NULL);
    ctrace_internal_write_flow_begin_event_record(NULL, NULL, NULL, 0u, NULL);
    ctrace_internal_write_flow_step_event_record(NULL, NULL, NULL, 0u, NULL);
    ctrace_internal_write_flow_end_event_record(NULL, NULL, NULL, 0u, NULL);
    ctrace_internal_write_kernel_object_record(NULL, ZX_HANDLE_INVALID, NULL);

    // Stubs for the public facing API.
    ctrace_write_instant_event_record(NULL, 0u, NULL, NULL, NULL, CTRACE_SCOPE_THREAD, NULL);
    ctrace_write_counter_event_record(NULL, 0u, NULL, NULL, NULL, 0u, NULL);
    ctrace_write_duration_begin_event_record(NULL, 0u, NULL, NULL, NULL, NULL);
    ctrace_write_duration_end_event_record(NULL, 0u, NULL, NULL, NULL, NULL);
    ctrace_write_async_begin_event_record(NULL, 0u, NULL, NULL, NULL, 0u, NULL);
    ctrace_write_async_instant_event_record(NULL, 0u, NULL, NULL, NULL, 0u, NULL);
    ctrace_write_async_end_event_record(NULL, 0u, NULL, NULL, NULL, 0u, NULL);
    ctrace_write_flow_begin_event_record(NULL, 0u, NULL, NULL, NULL, 0u, NULL);
    ctrace_write_flow_step_event_record(NULL, 0u, NULL, NULL, NULL, 0u, NULL);
    ctrace_write_flow_end_event_record(NULL, 0u, NULL, NULL, NULL, 0u, NULL);
    ctrace_write_kernel_object_record(NULL, ZX_HANDLE_INVALID, NULL);

    ctrace_writer_release(writer);
  }
}

}  // namespace
}  // namespace writer
}  // namespace tracing
