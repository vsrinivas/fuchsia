// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <unittest/unittest.h>

#include "fuzzer-fixture.h"
#include "test-fuzzer.h"

namespace fuzzing {
namespace testing {

#define ZXDEBUG 0

// Public methods

TestFuzzer::TestFuzzer()
    : out_(nullptr), outbuf_(nullptr), outbuflen_(0), err_(nullptr), errbuf_(nullptr),
      errbuflen_(0) {}

TestFuzzer::~TestFuzzer() {
    Reset();
}

void TestFuzzer::Reset() {
    Fuzzer::Reset();
    if (out_) {
        fclose(out_);
#if ZXDEBUG
        fprintf(stdout, "%s", outbuf_);
        fflush(stdout);
#endif
        free(outbuf_);
        outbuflen_ = 0;
        outbuf_ = nullptr;
        out_ = nullptr;
    }

    if (err_) {
        fclose(err_);
#if ZXDEBUG
        fprintf(stderr, "%s", errbuf_);
        fflush(stderr);
#endif
        free(errbuf_);
        errbuflen_ = 0;
        errbuf_ = nullptr;
        err_ = nullptr;
    }
}

bool TestFuzzer::InitZircon() {
    BEGIN_HELPER;
    ASSERT_TRUE(fixture_.CreateZircon());
    ASSERT_TRUE(Init());
    END_HELPER;
}

// Private methods

bool TestFuzzer::Init() {
    BEGIN_HELPER;
    Reset();

    out_ = open_memstream(&outbuf_, &outbuflen_);
    ASSERT_NONNULL(out_);

    err_ = open_memstream(&errbuf_, &errbuflen_);
    ASSERT_NONNULL(err_);

    // Configure base object
    set_out(out_);
    set_err(err_);

    END_HELPER;
}

} // namespace testing
} // namespace fuzzing
