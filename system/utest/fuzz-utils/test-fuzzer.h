// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdio.h>

#include <fuzz-utils/fuzzer.h>
#include <zircon/types.h>

#include "fuzzer-fixture.h"

namespace fuzzing {
namespace testing {

// |fuzzing::testing::Fuzzer| exposes internal APIs for testing and buffers output.
class TestFuzzer : public Fuzzer {
public:
    TestFuzzer();
    ~TestFuzzer() override;

    // Resets the out and err buffers to be unallocated.
    void Reset() override;

    // Sets up the test fuzzer to buffer output with a Zircon-standalone test fixture
    bool InitZircon();

    // Returns the value associated with the given |key|, or null if unset.
    const char* GetOption(const char* key) { return options().get(key); }

    // Expose parent class methods
    zx_status_t SetOption(const char* option) { return Fuzzer::SetOption(option); }
    zx_status_t SetOption(const char* key, const char* val) { return Fuzzer::SetOption(key, val); }

private:
    // Sets up the test fuzzer to buffer output without changing the test fixture
    bool Init();

    // The current test fixture
    FuzzerFixture fixture_;

    // Output stream
    FILE* out_;
    char* outbuf_;
    size_t outbuflen_;

    // Error stream
    FILE* err_;
    char* errbuf_;
    size_t errbuflen_;
};

} // namespace testing
} // namespace fuzzing
