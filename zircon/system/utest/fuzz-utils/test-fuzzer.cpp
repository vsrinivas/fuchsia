// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <fbl/auto_call.h>
#include <fbl/string_printf.h>
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
    args_.clear();

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

bool TestFuzzer::Init() {
    BEGIN_HELPER;
    ASSERT_TRUE(fixture_.Create());
    Reset();

    out_ = open_memstream(&outbuf_, &outbuflen_);
    ASSERT_NONNULL(out_);

    err_ = open_memstream(&errbuf_, &errbuflen_);
    ASSERT_NONNULL(err_);

    // Configure base object
    set_root(fixture_.path());
    set_out(out_);
    set_err(err_);

    END_HELPER;
}

zx_status_t TestFuzzer::Eval(const char* cmdline) {
    BEGIN_HELPER;
    ASSERT_TRUE(Init());

    char* buf = strdup(cmdline);
    ASSERT_NONNULL(buf);
    auto cleanup = fbl::MakeAutoCall([&buf]() { free(buf); });
    char* ptr = buf;
    char* arg;
    while ((arg = strsep(&ptr, " "))) {
        if (arg && *arg) {
            args_.push_back(arg);
        }
    }

    END_HELPER;
}

bool TestFuzzer::InStdOut(const char* needle) {
    fflush(out_);
    return strcasestr(outbuf_, needle) != nullptr;
}

bool TestFuzzer::InStdErr(const char* needle) {
    fflush(err_);
    return strcasestr(errbuf_, needle) != nullptr;
}

int TestFuzzer::FindArg(const char* fmt, const fbl::String& arg) {
    fbl::StringBuffer<PATH_MAX> buffer;
    buffer.AppendPrintf(fmt, arg.c_str());
    int result = 0;
    for (const char* arg = args_.first(); arg; arg = args_.next()) {
        if (strcmp(arg, buffer.c_str()) == 0) {
            return result;
        }
        ++result;
    }
    return -1;
}

bool TestFuzzer::CheckProcess(zx_handle_t process, const char* target) {
    if (target) {
        set_target(target);
    }
    return Fuzzer::CheckProcess(process);
}

// Protected methods

zx_status_t TestFuzzer::Execute() {
    GetArgs(&args_);
    return ZX_OK;
}

} // namespace testing
} // namespace fuzzing
