// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdio.h>

#include <fuzz-utils/fuzzer.h>
#include <fuzz-utils/path.h>
#include <zircon/types.h>

#include "fuzzer-fixture.h"

namespace fuzzing {
namespace testing {

// |fuzzing::testing::Fuzzer| exposes internal APIs for testing and buffers output.
class TestFuzzer : public Fuzzer {
public:
    TestFuzzer();
    ~TestFuzzer() override;

    const FuzzerFixture& fixture() const { return fixture_; }

    // Resets the out and err buffers to be unallocated.
    void Reset() override;

    // Sets up the test fuzzer to buffer output with a test fixture of Fuchsia packages
    bool Init();

    // Resets |test| and reconstructs it from the |cmdline| in the context of the current fixture.
    zx_status_t Eval(const char* cmdline);

    // Returns the value associated with the given |key|, or null if unset.
    const char* GetOption(const char* key) { return options().get(key); }

    // Invoke the base method with the saved arguments.
    zx_status_t Run() { return Fuzzer::Run(&args_); }

    // Checks if the (case-insensitive) substring is in the buffered output
    bool InStdOut(const char* needle);
    bool InStdErr(const char* needle);

    // Returns the index in "argv" of the arg produced from |fmt| and any variadic parameters, or -1
    // if it isn't found.
    int FindArg(const char* fmt, const fbl::String& arg);
    int FindArg(const fbl::String& arg) { return FindArg("%s", arg); }

    // Various fixture locations
    const char* executable() const { return executable_.c_str(); }
    const char* manifest() const { return manifest_.c_str(); }
    const char* dictionary() const { return dictionary_.c_str(); }
    const char* data_path() const { return data_path_.c_str(); }
    fbl::String data_path(const char* relpath) { return data_path_.Join(relpath); }

    // Expose parent class methods
    zx_status_t SetOption(const fbl::String& option) { return Fuzzer::SetOption(option); }
    zx_status_t SetOption(const fbl::String& key, const fbl::String& val) {
        return Fuzzer::SetOption(key, val);
    }
    zx_status_t RebasePath(const fbl::String& package, Path* out) {
        return Fuzzer::RebasePath(package, out);
    }
    zx_status_t GetPackagePath(const fbl::String& package, Path* out) {
        return Fuzzer::GetPackagePath(package, out);
    }
    void FindFuzzers(const fbl::String& package, const fbl::String& target, StringMap* out) {
        Fuzzer::FindFuzzers(package, target, out);
    }
    void FindFuzzers(const fbl::String& name, StringMap* out) { Fuzzer::FindFuzzers(name, out); }

    // Exposes |Fuzzer::CheckProcess| optionally overriding the executable name to look for.
    bool CheckProcess(zx_handle_t process, const char* executable = nullptr);

protected:
    // Overrides |Fuzzer::Execute| to simply save the subprocess' command line without spawning it.
    zx_status_t Execute() override;

private:
    // The current test fixture
    FuzzerFixture fixture_;

    // The arguments passed to the subprocess
    StringList args_;

    // Test info, captured by |Execute|
    fbl::String executable_;
    fbl::String manifest_;
    fbl::String dictionary_;
    Path data_path_;

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
