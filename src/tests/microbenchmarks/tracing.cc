// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/trace/event.h>

#include <vector>

#include <perftest/perftest.h>

namespace {

// This benchmark aims to measure the overhead of adding tracing to code. It's
// called by the end to end framework runner with and without tracing enabled
// so that the results can be compared.
//
// There are several different aspects of tracing that we would like to measure:
//
// # Tracing Enabled vs Tracing Disabled
//
// Tracing is done via adding lines such as TRACE_INSTANT into the code. If
// tracing is off, these should theoretically be cheap or free
//
// # Tracing Enabled, Category disabled
//
// If tracing is on, but the category the trace event emits is disabled, then
// we should expect the trace event to be cheap or free.

bool InstantEventTest() {
  TRACE_INSTANT("benchmark", "InstantEvent", TRACE_SCOPE_THREAD);
  return true;
}

bool InstantEventArgsTest() {
  TRACE_INSTANT("benchmark", "InstantEventArgs", TRACE_SCOPE_THREAD, "arg1", 1, "arg2", 2, "arg3",
                3);
  return true;
}

bool ScopedDurationEventTest() {
  TRACE_DURATION("benchmark", "ScopedDuration");
  return true;
}

bool BeginEndDurationEventTest() {
  TRACE_DURATION_BEGIN("benchmark", "DurationBegin");
  TRACE_DURATION_END("benchmark", "DurationEnd");
  return true;
}

void RegisterTests() {
  perftest::RegisterSimpleTest<InstantEventTest>("Tracing/InstantEvent");
  perftest::RegisterSimpleTest<InstantEventArgsTest>("Tracing/InstantEventArgs");
  perftest::RegisterSimpleTest<ScopedDurationEventTest>("Tracing/ScopedDurationEvent");
  perftest::RegisterSimpleTest<BeginEndDurationEventTest>("Tracing/BeginEndDurationEvent");
}
PERFTEST_CTOR(RegisterTests)

}  // namespace
