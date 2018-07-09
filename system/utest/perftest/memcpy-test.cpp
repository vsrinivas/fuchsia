// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <fbl/string_printf.h>
#include <fbl/unique_ptr.h>
#include <perftest/perftest.h>

namespace {

// Test performance of memcpy() on a block of the given size.
bool MemcpyTest(perftest::RepeatState* state, size_t size) {
    state->SetBytesProcessedPerRun(size);

    fbl::unique_ptr<char[]> src(new char[size]);
    fbl::unique_ptr<char[]> dest(new char[size]);
    // Initialize src so that we are not copying from uninitialized memory.
    memset(src.get(), 0, size);

    while (state->KeepRunning()) {
        memcpy(dest.get(), src.get(), size);
        // Stop the compiler from optimizing away the memcpy() call.
        perftest::DoNotOptimize(src.get());
        perftest::DoNotOptimize(dest.get());
    }
    return true;
}

void RegisterTests() {
    static const size_t kSizesBytes[] = {
        1000,
        100000,
    };
    for (auto size : kSizesBytes) {
        auto name = fbl::StringPrintf("Memcpy/%zubytes", size);
        perftest::RegisterTest(name.c_str(), MemcpyTest, size);
    }
}
PERFTEST_CTOR(RegisterTests);

}  // namespace
