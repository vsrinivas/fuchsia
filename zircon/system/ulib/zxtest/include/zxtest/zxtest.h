// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// Select the right implementation.
#ifdef __cplusplus
#include <zxtest/cpp/zxtest.h>
#else
#include <zxtest/c/zxtest.h>
#endif

#define _ASSERT_PTR(op, expected, actual, fatal, file, line, desc, ...)                            \
    _ASSERT_VAR_COERCE(op, expected, actual, _ZXTEST_AUTO_VAR_TYPE(actual), fatal, file, line,     \
                       desc, ##__VA_ARGS__)

#define ASSERT_EQ(val2, val1, ...)                                                                 \
    _ASSERT_VAR(_EQ, val2, val1, true, __FILE__, __LINE__, "Expected " #val1 " == " #val2 ".",     \
                ##__VA_ARGS__)

#define ASSERT_NE(val2, val1, ...)                                                                 \
    _ASSERT_VAR(_NE, val2, val1, true, __FILE__, __LINE__, "Expected " #val1 " != " #val2 ".",     \
                ##__VA_ARGS__)

#define EXPECT_EQ(val2, val1, ...)                                                                 \
    _ASSERT_VAR(_EQ, val2, val1, false, __FILE__, __LINE__, "Expected " #val1 " == " #val2 ".",    \
                ##__VA_ARGS__)

#define EXPECT_NE(val2, val1, ...)                                                                 \
    _ASSERT_VAR(_NE, val2, val1, false, __FILE__, __LINE__, "Expected " #val1 " != " #val2 ".",    \
                ##__VA_ARGS__)

#define ASSERT_LT(val1, val2, ...)                                                                 \
    _ASSERT_VAR(_LT, val2, val1, true, __FILE__, __LINE__, "Expected " #val1 " < " #val2 ".",      \
                ##__VA_ARGS__)

#define ASSERT_LE(val1, val2, ...)                                                                 \
    _ASSERT_VAR(_LE, val2, val1, true, __FILE__, __LINE__, "Expected " #val1 " <= " #val2 ".",     \
                ##__VA_ARGS__)

#define EXPECT_LT(val1, val2, ...)                                                                 \
    _ASSERT_VAR(_LT, val2, val1, false, __FILE__, __LINE__, "Expected " #val1 " < " #val2 ".",     \
                ##__VA_ARGS__)

#define EXPECT_LE(val1, val2, ...)                                                                 \
    _ASSERT_VAR(_LE, val2, val1, false, __FILE__, __LINE__, "Expected " #val1 " <= " #val2 ".",    \
                ##__VA_ARGS__)

#define ASSERT_GT(val1, val2, ...)                                                                 \
    _ASSERT_VAR(_GT, val2, val1, true, __FILE__, __LINE__, "Expected " #val1 " > " #val2 ".",      \
                ##__VA_ARGS__)

#define ASSERT_GE(val1, val2, ...)                                                                 \
    _ASSERT_VAR(_GE, val2, val1, true, __FILE__, __LINE__, "Expected " #val1 " >= " #val2 ".",     \
                ##__VA_ARGS__)

#define EXPECT_GT(val1, val2, ...)                                                                 \
    _ASSERT_VAR(_GT, val2, val1, false, __FILE__, __LINE__, "Expected " #val1 " > " #val2 ".",     \
                ##__VA_ARGS__)

#define EXPECT_GE(val1, val2, ...)                                                                 \
    _ASSERT_VAR(_GE, val2, val1, false, __FILE__, __LINE__, "Expected " #val1 " >= " #val2 ".",    \
                ##__VA_ARGS__)

#define ASSERT_STR_EQ(val2, val1, ...)                                                             \
    _ASSERT_VAR(_STREQ, val2, val1, true, __FILE__, __LINE__,                                      \
                "Expected strings " #val1 " == " #val2 ".", ##__VA_ARGS__)

#define EXPECT_STR_EQ(val2, val1, ...)                                                             \
    _ASSERT_VAR(_STREQ, val2, val1, false, __FILE__, __LINE__,                                     \
                "Expected strings " #val1 " == " #val2 ".", ##__VA_ARGS__)

#define ASSERT_STR_NE(val2, val1, ...)                                                             \
    _ASSERT_VAR(_STRNE, val2, val1, true, __FILE__, __LINE__,                                      \
                "Expected strings " #val1 " != " #val2 ".", ##__VA_ARGS__)

#define EXPECT_STR_NE(val2, val1, ...)                                                             \
    _ASSERT_VAR(_STRNE, val2, val1, false, __FILE__, __LINE__,                                     \
                "Expected strings " #val1 " != " #val2 ".", ##__VA_ARGS__)

// Used to evaluate _ZXTEST_NULLPTR to an actual value.
#define _ASSERT_PTR_DELEGATE(...) _ASSERT_PTR(__VA_ARGS__)

#define ASSERT_NULL(val1, ...)                                                                     \
    _ASSERT_PTR_DELEGATE(_EQ, _ZXTEST_NULLPTR, val1, true, __FILE__, __LINE__,                     \
                         "Expected " #val1 " is null pointer.", ##__VA_ARGS__)

#define EXPECT_NULL(val1, ...)                                                                     \
    _ASSERT_PTR_DELEGATE(_EQ, _ZXTEST_NULLPTR, val1, false, __FILE__, __LINE__,                    \
                         "Expected " #val1 " is null pointer.", ##__VA_ARGS__)

#define ASSERT_NOT_NULL(val1, ...)                                                                 \
    _ASSERT_PTR_DELEGATE(_NE, _ZXTEST_NULLPTR, val1, true, __FILE__, __LINE__,                     \
                         "Expected " #val1 " non null pointer.", ##__VA_ARGS__)

#define EXPECT_NOT_NULL(val1, ...)                                                                 \
    _ASSERT_PTR_DELEGATE(_NE, _ZXTEST_NULLPTR, val1, false, __FILE__, __LINE__,                    \
                         "Expected " #val1 " non null pointer.", ##__VA_ARGS__)

#define ASSERT_OK(val1, ...)                                                                       \
    _ASSERT_VAR_STATUS(_LE, val1, ZX_OK, true, __FILE__, __LINE__, "Expected " #val1 " is ZX_OK.", \
                       ##__VA_ARGS__)

#define EXPECT_OK(val1, ...)                                                                       \
    _ASSERT_VAR_STATUS(_LE, val1, ZX_OK, false, __FILE__, __LINE__,                                \
                       "Expected " #val1 " is ZX_OK.", ##__VA_ARGS__)

#define ASSERT_NOT_OK(val1, ...)                                                                   \
    _ASSERT_VAR(_GT, val1, ZX_OK, true, __FILE__, __LINE__, "Expected " #val1 " is not ZX_OK.",    \
                ##__VA_ARGS__)

#define EXPECT_NOT_OK(val1, ...)                                                                   \
    _ASSERT_VAR(_GT, val1, ZX_OK, false, __FILE__, __LINE__, "Expected " #val1 " is not ZX_OK.",   \
                ##__VA_ARGS__)

#define ASSERT_BYTES_EQ(val1, val2, size, ...)                                                     \
    _ASSERT_VAR_BYTES(_BYTEEQ, val2, val1, size, true, __FILE__, __LINE__,                         \
                      "Expected " #val1 " same bytes as " #val2 ".", ##__VA_ARGS__)

#define EXPECT_BYTES_EQ(val1, val2, size, ...)                                                     \
    _ASSERT_VAR_BYTES(_BYTEEQ, val2, val1, size, false, __FILE__, __LINE__,                        \
                      "Expected " #val1 " same bytes as " #val2 ".", ##__VA_ARGS__)

#define ASSERT_BYTES_NE(val1, val2, size, ...)                                                     \
    _ASSERT_VAR_BYTES(_BYTENE, val2, val1, size, true, __FILE__, __LINE__,                         \
                      "Expected " #val1 " same bytes as " #val2 ".", ##__VA_ARGS__)

#define EXPECT_BYTES_NE(val1, val2, size, ...)                                                     \
    _ASSERT_VAR_BYTES(_BYTENE, val2, val1, size, false, __FILE__, __LINE__,                        \
                      "Expected " #val1 " same bytes as " #val2 ".", ##__VA_ARGS__)

#define ASSERT_TRUE(val, ...)                                                                      \
    _ASSERT_VAR(_BOOL, val, true, true, __FILE__, __LINE__, "Expected " #val " is true.",          \
                ##__VA_ARGS__)

#define ASSERT_FALSE(val, ...)                                                                     \
    _ASSERT_VAR(_BOOL, val, false, true, __FILE__, __LINE__, "Expected " #val " is false.",        \
                ##__VA_ARGS__)

#define EXPECT_TRUE(val, ...)                                                                      \
    _ASSERT_VAR(_BOOL, val, true, false, __FILE__, __LINE__, "Expected " #val " is true.",         \
                ##__VA_ARGS__)

#define EXPECT_FALSE(val, ...)                                                                     \
    _ASSERT_VAR(_BOOL, val, false, false, __FILE__, __LINE__, "Expected " #val " is false.",       \
                ##__VA_ARGS__)

#define FAIL(...)                                                                                  \
    do {                                                                                           \
        _ZXTEST_FAIL_NO_RETURN(true, "", ##__VA_ARGS__);                                           \
        return;                                                                                    \
    } while (0)

#define ADD_FAILURE(...) _ZXTEST_FAIL_NO_RETURN(false, "", ##__VA_ARGS__)

#define ADD_FATAL_FAILURE(...) _ZXTEST_FAIL_NO_RETURN(true, "", ##__VA_ARGS__)

#define ASSERT_NO_FATAL_FAILURES(statement, ...)                                                   \
    do {                                                                                           \
        statement;                                                                                 \
        _ZXTEST_ASSERT_ERROR(_ZXTEST_ABORT_IF_ERROR, true,                                         \
                             "Test registered fatal failures in " #statement ".", ##__VA_ARGS__);  \
    } while (0)

#define ASSERT_NO_FAILURES(statement, ...)                                                         \
    do {                                                                                           \
        statement;                                                                                 \
        _ZXTEST_ASSERT_ERROR(_ZXTEST_TEST_HAS_ERRORS, true,                                        \
                             "Test registered failures in " #statement ".", ##__VA_ARGS__);        \
    } while (0)

#ifdef __Fuchsia__

// In cpp |statement| is allowed to be a lambda. To prevent the pre processor from tokenizing lambda
// captures or multiple argument declaration, wrap the lambda declaration in ().
//
// E.g.:
// int a, b;
// ASSERT_DEATH(([&a,&b] {
//     int c,d;
//     CrashNow(a,b, &c, &d);
// }, "Failed to crash now %d %d\n", a, b);
#define ASSERT_DEATH(statement, ...)                                                               \
    _ZXTEST_DEATH_STATEMENT(statement, _ZXTEST_DEATH_STATUS_EXCEPTION,                             \
                            "Exception was never raised.", ##__VA_ARGS__)

// In cpp |statement| is allowed to be a lambda. To prevent the pre processor from tokenizing lambda
// captures or multiple argument declaration, wrap the lambda declaration in ().
//
// E.g.:
// int a, b;
// ASSERT_DEATH(([&a,&b] {
//     int c,d;
//     CrashNow(a,b, &c, &d);
// }, "Failed to crash now %d %d\n", a, b);
#define ASSERT_NO_DEATH(statement, ...)                                                            \
    _ZXTEST_DEATH_STATEMENT(statement, _ZXTEST_DEATH_STATUS_COMPLETE,                              \
                            "Unexpected exception was raised.", ##__VA_ARGS__)
#endif
