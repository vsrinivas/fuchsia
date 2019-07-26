// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "benchmarks.h"

#include <stdio.h>

#define NTRACE
#include <trace-vthread/event_vthread.h>
#include <trace/event.h>

#include "runner.h"

static void NullSetup() {}
static void NullTeardown() {}

#define NTRACE_DURATION_TEST(DURATION_MACRO)                                                     \
  RunAndMeasure(                                                                                 \
      #DURATION_MACRO " macro with 0 arguments", "NTRACE",                                       \
      [] { DURATION_MACRO("+enabled", "name"); }, NullSetup, NullTeardown);                      \
                                                                                                 \
  RunAndMeasure(                                                                                 \
      #DURATION_MACRO " macro with 1 int32 argument", "NTRACE",                                  \
      [] { DURATION_MACRO("+enabled", "name", "k1", 1); }, NullSetup, NullTeardown);             \
                                                                                                 \
  RunAndMeasure(                                                                                 \
      #DURATION_MACRO " macro with 4 int32 arguments", "NTRACE",                                 \
      [] { DURATION_MACRO("+enabled", "name", "k1", 1, "k2", 2, "k3", 3, "k4", 4); }, NullSetup, \
      NullTeardown);                                                                             \
                                                                                                 \
  RunAndMeasure(                                                                                 \
      #DURATION_MACRO " macro with 8 int32 arguments", "NTRACE",                                 \
      [] {                                                                                       \
        DURATION_MACRO("+enabled", "name", "k1", 1, "k2", 2, "k3", 3, "k4", 4, "k5", 5, "k6", 6, \
                       "k7", 7, "k8", 8);                                                        \
      },                                                                                         \
      NullSetup, NullTeardown);

void RunNoTraceBenchmarks() {
  RunAndMeasure(
      "TRACE_ENABLED", "NTRACE", [] { ZX_DEBUG_ASSERT(!TRACE_ENABLED()); }, NullSetup,
      NullTeardown);

  RunAndMeasure(
      "TRACE_CATEGORY_ENABLED", "NTRACE",
      [] { ZX_DEBUG_ASSERT(!TRACE_CATEGORY_ENABLED("+enabled")); }, NullSetup, NullTeardown);

  NTRACE_DURATION_TEST(TRACE_DURATION_BEGIN);
  NTRACE_DURATION_TEST(TRACE_DURATION);

  RunAndMeasure(
      "TRACE_VTHREAD_DURATION_BEGIN macro with 0 arguments", "NTRACE",
      [] { TRACE_VTHREAD_DURATION_BEGIN("+enabled", "name", "vthread", 1, zx_ticks_get()); },
      NullSetup, NullTeardown);
}
