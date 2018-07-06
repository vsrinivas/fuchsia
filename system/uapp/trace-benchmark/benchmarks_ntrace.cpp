// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "benchmarks.h"

#include <stdio.h>

#define NTRACE
#include <trace/event.h>

#include "runner.h"

void RunNoTraceBenchmarks() {
    puts("Running benchmarks with NTRACE...\n");

    RunAndMeasure("TRACE_ENABLED with NTRACE", [] {
        ZX_DEBUG_ASSERT(!TRACE_ENABLED());
    });

    RunAndMeasure("TRACE_CATEGORY_ENABLED with NTRACE", [] {
        ZX_DEBUG_ASSERT(!TRACE_CATEGORY_ENABLED("+enabled"));
    });

    RunAndMeasure("TRACE_DURATION_BEGIN macro with 0 arguments with NTRACE", [] {
        TRACE_DURATION_BEGIN("+enabled", "name");
    });

    RunAndMeasure("TRACE_DURATION_BEGIN macro with 1 int32 argument with NTRACE", [] {
        TRACE_DURATION_BEGIN("+enabled", "name",
                             "k1", 1);
    });

    RunAndMeasure("TRACE_DURATION_BEGIN macro with 4 int32 arguments with NTRACE", [] {
        TRACE_DURATION_BEGIN("+enabled", "name",
                             "k1", 1, "k2", 2, "k3", 3, "k4", 4);
    });

    RunAndMeasure("TRACE_DURATION_BEGIN macro with 8 int32 arguments with NTRACE", [] {
        TRACE_DURATION_BEGIN("+enabled", "name",
                             "k1", 1, "k2", 2, "k3", 3, "k4", 4,
                             "k5", 5, "k6", 6, "k7", 7, "k8", 8);
    });
}
