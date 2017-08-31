// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mxtl/string_printf.h>

#include <stdarg.h>

#include <mxtl/array.h>
#include <unittest/unittest.h>

#define EXPECT_CSTR_EQ(expected, actual) \
    EXPECT_STR_EQ(expected, actual, strlen(expected) + 1u, "unequal cstr")

namespace {

// Note: |runnable| can't be a reference since that'd make the behavior of
// |va_start()| undefined.
template <typename Runnable>
mxtl::String VAListHelper(Runnable runnable, ...) {
    va_list ap;
    va_start(ap, runnable);
    mxtl::String rv = runnable(ap);
    va_end(ap);
    return rv;
}

bool string_printf_basic_test() {
    BEGIN_TEST;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-zero-length"
    EXPECT_CSTR_EQ("", mxtl::StringPrintf("").c_str());
#pragma GCC diagnostic pop
    EXPECT_CSTR_EQ("hello", mxtl::StringPrintf("hello").c_str());
    EXPECT_CSTR_EQ("hello-123", mxtl::StringPrintf("hello%d", -123).c_str());
    EXPECT_CSTR_EQ("hello0123FACE", mxtl::StringPrintf("%s%04d%X", "hello", 123, 0xfaceU).c_str());

    END_TEST;
}

bool string_vprintf_basic_test() {
    BEGIN_TEST;

    EXPECT_CSTR_EQ("",
                   VAListHelper([](va_list ap) -> mxtl::String {
                       return mxtl::StringVPrintf("", ap);
                   })
                       .c_str());
    EXPECT_CSTR_EQ("hello",
                   VAListHelper([](va_list ap) -> mxtl::String {
                       return mxtl::StringVPrintf("hello", ap);
                   })
                       .c_str());
    EXPECT_CSTR_EQ("hello-123",
                   VAListHelper(
                       [](va_list ap) -> mxtl::String {
                           return mxtl::StringVPrintf("hello%d", ap);
                       },
                       -123)
                       .c_str());
    EXPECT_CSTR_EQ("hello0123FACE",
                   VAListHelper(
                       [](va_list ap) -> mxtl::String {
                           return mxtl::StringVPrintf("%s%04d%X", ap);
                       },
                       "hello", 123, 0xfaceU)
                       .c_str());

    END_TEST;
}

// Generally, we assume that everything forwards to |mxtl::StringVPrintf()|, so
// testing |mxtl::StringPrintf()| more carefully suffices.

bool string_printf_boundary_test() {
    BEGIN_TEST;

    // Note: The size of strings generated should cover the boundary cases in the
    // constant |kStackBufferSize| in |StringVPrintf()|.
    for (size_t i = 800; i < 1200; i++) {
        mxtl::String stuff(i, 'x');
        mxtl::String format = mxtl::String::Concat({stuff, "%d", "%s", " world"});
        EXPECT_CSTR_EQ(mxtl::String::Concat({stuff, "123", "hello world"}).c_str(),
                       mxtl::StringPrintf(format.c_str(), 123, "hello").c_str());
    }

    END_TEST;
}

bool string_printf_very_big_string_test() {
    BEGIN_TEST;

    // 4 megabytes of exes (we'll generate 5 times this).
    mxtl::String stuff(4u << 20u, 'x');
    mxtl::String format = mxtl::String::Concat({"%s", stuff, "%s", stuff, "%s"});
    EXPECT_CSTR_EQ(mxtl::String::Concat({stuff, stuff, stuff, stuff, stuff}).c_str(),
                   mxtl::StringPrintf(format.c_str(), stuff.c_str(), stuff.c_str(),
                                      stuff.c_str())
                       .c_str());

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(string_printf_tests)
RUN_TEST(string_printf_basic_test)
RUN_TEST(string_vprintf_basic_test)
RUN_TEST(string_printf_boundary_test)
RUN_TEST(string_printf_very_big_string_test)
END_TEST_CASE(string_printf_tests)
