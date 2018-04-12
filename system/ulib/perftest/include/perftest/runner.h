// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/vector.h>
#include <perftest/perftest.h>

namespace perftest {
namespace internal {

// Definitions used by the perf test runner.  These are in a header file so
// that the perf test runner can be tested by unit tests.

struct NamedTest {
    fbl::String name;
    fbl::Function<TestFunc> test_func;
};

typedef fbl::Vector<NamedTest> TestList;

bool RunTests(TestList* test_list, uint32_t run_count, const char* regex_string,
              FILE* log_stream, ResultsSet* results_set);

struct CommandArgs {
    const char* output_filename = nullptr;
    // Note that this default matches any string.
    const char* filter_regex = "";
    uint32_t run_count = 1000;
    bool enable_tracing = false;
    double startup_delay_seconds = 0;
};

void ParseCommandArgs(int argc, char** argv, CommandArgs* dest);

}  // namespace internal
}  // namespace perftest
