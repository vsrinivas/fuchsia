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
    package_path_.clear();
    data_path_.clear();
    executable_.clear();
    dictionary_.clear();

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

bool TestFuzzer::InitFuchsia() {
    BEGIN_HELPER;
    ASSERT_TRUE(fixture_.CreateFuchsia());
    ASSERT_TRUE(Init());
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

int TestFuzzer::FindArg(const char* fmt, ...) {
    fbl::StringBuffer<PATH_MAX> buffer;
    va_list ap;
    va_start(ap, fmt);
    buffer.AppendVPrintf(fmt, ap);
    va_end(ap);
    int result = 0;
    for (const char* arg = args_.first(); arg; arg = args_.next()) {
        if (strcmp(arg, buffer.c_str()) == 0) {
            return result;
        }
        ++result;
    }
    return -1;
}

bool TestFuzzer::CheckProcess(zx_handle_t process, const char* executable) {
    if (executable) {
        set_executable(executable);
    }
    return Fuzzer::CheckProcess(process);
}

// Protected methods

zx_status_t TestFuzzer::Execute(bool wait_for_completion) {
    GetArgs(&args_);

    const char* arg0 = args_.first();
    char* str = strdup(arg0 + strlen(fixture_.path().c_str()));
    ZX_ASSERT(str);
    auto cleanup = fbl::MakeAutoCall([str]() { free(str); });

    const char *package, *version, *target;
    if (strcmp(strsep(&str, "/"), "pkgfs") != 0) {
        package = "zircon_fuzzers";
        version = "0";
        strsep(&str, "/");
        strsep(&str, "/");
        target = strsep(&str, "/");
    } else {
        strsep(&str, "/");
        package = strsep(&str, "/");
        version = strsep(&str, "/");
        strsep(&str, "/");
        target = strsep(&str, "/");
    }
    ZX_ASSERT(package);
    ZX_ASSERT(version);
    ZX_ASSERT(target);
    executable_.Set(arg0);
    data_path_.Set(fixture_.path("data/fuzzing/%s/%s", package, target).c_str());
    dictionary_.Set(
        fixture_.path("pkgfs/packages/%s/%s/data/%s/dictionary", package, version, target).c_str());
    package_path_.Set(fixture_.path("pkgfs/packages/%s/%s", package, version).c_str());

    return ZX_OK;
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
    set_root(fixture_.path().c_str());
    set_out(out_);
    set_err(err_);

    END_HELPER;
}

} // namespace testing
} // namespace fuzzing
