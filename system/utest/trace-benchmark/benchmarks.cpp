// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "benchmarks.h"

#include <stdio.h>

#include <magenta/assert.h>

#include <trace-engine/instrumentation.h>
#include <trace/event.h>

namespace {

void RunBenchmarks(bool tracing_enabled) {
    Run("is enabled", [] {
        trace_is_enabled();
    });

    Run("is category enabled", [] {
        trace_is_category_enabled("+enabled");
    });

    if (tracing_enabled) {
        Run("is category enabled for disabled category", [] {
            trace_is_category_enabled("-disabled");
        });
    }

    Run("acquire / release context", [] {
        trace_context_t* context = trace_acquire_context();
        if (unlikely(context))
            trace_release_context(context);
    });

    Run("acquire / release context for category", [] {
        trace_string_ref_t category_ref;
        trace_context_t* context = trace_acquire_context_for_category(
            "+enabled", &category_ref);
        if (unlikely(context))
            trace_release_context(context);
    });

    if (tracing_enabled) {
        Run("acquire / release context for disabled category", [] {
            trace_string_ref_t category_ref;
            trace_context_t* context = trace_acquire_context_for_category(
                "-disabled", &category_ref);
            MX_DEBUG_ASSERT(!context);
        });
    }

    Run("TRACE_DURATION_BEGIN macro with 0 arguments", [] {
        TRACE_DURATION_BEGIN("+enabled", "name");
    });

    Run("TRACE_DURATION_BEGIN macro with 1 int32 argument", [] {
        TRACE_DURATION_BEGIN("+enabled", "name",
                             "k1", 1);
    });

    Run("TRACE_DURATION_BEGIN macro with 1 double argument", [] {
        TRACE_DURATION_BEGIN("+enabled", "name",
                             "k1", 1.);
    });

    Run("TRACE_DURATION_BEGIN macro with 1 string argument", [] {
        TRACE_DURATION_BEGIN("+enabled", "name",
                             "k1", "string1");
    });

    Run("TRACE_DURATION_BEGIN macro with 4 int32 arguments", [] {
        TRACE_DURATION_BEGIN("+enabled", "name",
                             "k1", 1, "k2", 2, "k3", 3, "k4", 4);
    });

    Run("TRACE_DURATION_BEGIN macro with 4 double arguments", [] {
        TRACE_DURATION_BEGIN("+enabled", "name",
                             "k1", 1., "k2", 2., "k3", 3., "k4", 4.);
    });

    Run("TRACE_DURATION_BEGIN macro with 4 string arguments", [] {
        TRACE_DURATION_BEGIN("+enabled", "name",
                             "k1", "string1", "k2", "string2", "k3", "string3", "k4", "string4");
    });

    Run("TRACE_DURATION_BEGIN macro with 8 int32 arguments", [] {
        TRACE_DURATION_BEGIN("+enabled", "name",
                             "k1", 1, "k2", 2, "k3", 3, "k4", 4,
                             "k5", 5, "k6", 6, "k7", 7, "k8", 8);
    });

    Run("TRACE_DURATION_BEGIN macro with 8 double arguments", [] {
        TRACE_DURATION_BEGIN("+enabled", "name",
                             "k1", 1., "k2", 2., "k3", 3., "k4", 4.,
                             "k5", 4., "k6", 5., "k7", 7., "k8", 8.);
    });

    Run("TRACE_DURATION_BEGIN macro with 8 string arguments", [] {
        TRACE_DURATION_BEGIN("+enabled", "name",
                             "k1", "string1", "k2", "string2", "k3", "string3", "k4", "string4",
                             "k5", "string5", "k6", "string6", "k7", "string7", "k8", "string8");
    });

    if (tracing_enabled) {
        Run("TRACE_DURATION_BEGIN macro with 0 arguments for disabled category", [] {
            TRACE_DURATION_BEGIN("-disabled", "name");
        });

        Run("TRACE_DURATION_BEGIN macro with 1 int32 argument for disabled category", [] {
            TRACE_DURATION_BEGIN("-disabled", "name",
                                 "k1", 1);
        });

        Run("TRACE_DURATION_BEGIN macro with 4 int32 arguments for disabled category", [] {
            TRACE_DURATION_BEGIN("-disabled", "name",
                                 "k1", 1, "k2", 2, "k3", 3, "k4", 4);
        });

        Run("TRACE_DURATION_BEGIN macro with 8 int32 arguments for disabled category", [] {
            TRACE_DURATION_BEGIN("-disabled", "name",
                                 "k1", 1, "k2", 2, "k3", 3, "k4", 4,
                                 "k5", 5, "k6", 6, "k7", 7, "k8", 8);
        });
    }
}

} // namespace

void RunTracingDisabledBenchmarks() {
    puts("Running benchmarks with tracing disabled...\n");
    RunBenchmarks(false);
}

void RunTracingEnabledBenchmarks() {
    puts("Running benchmarks with tracing enabled...\n");
    RunBenchmarks(true);
}
