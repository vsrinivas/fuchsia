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

// Each header defines a couple of macros for allocating buffers for prnting variables,
// asserting, and checking continuation.
#define _EQ(actual, expected) actual == expected
#define _NE(actual, expected) actual != expected
#define _LT(actual, expected) actual < expected
#define _LE(actual, expected) actual <= expected
#define _GT(actual, expected) actual > expected
#define _GE(actual, expected) actual >= expected
#define _STREQ(actual, expected) _ZXTEST_STRCMP(actual, expected)
#define _STRNE(actual, expected) !_ZXTEST_STRCMP(actual, expected)
#define _BYTEEQ(actual, expected, size) memcmp(actual, expected, size) == 0
#define _BYTENE(actual, expected, size) memcmp(actual, expected, size) != 0

// Used to cleanup allocated buffers for formatted messages.
// Marked unused to prevent compiler warnings.
static void zxtest_clean_buffer(char** buffer) __attribute__((unused));
static void zxtest_clean_buffer(char** buffer) {
    free(*buffer);
}

// Used for selecting right macro impl.
#define _ZXTEST_SEL_N(_0, _1, _2, FN, ...) FN

#define _GEN_ASSERT_DESC_0(buffer, buffer_size, desc) snprintf(buffer, buffer_size, desc)

#define _GEN_ASSERT_DESC_1(buffer, buffer_size, desc, msg, ...)                                    \
    snprintf(buffer, buffer_size, desc " " msg, ##__VA_ARGS__)

#define _GEN_ASSERT_DESC(buffer, buffer_size, desc, ...)                                           \
    _ZXTEST_SEL_N(_0, ##__VA_ARGS__, _GEN_ASSERT_DESC_1, _GEN_ASSERT_DESC_1, _GEN_ASSERT_DESC_0)   \
    (buffer, buffer_size, desc, ##__VA_ARGS__)

#define _LOAD_BUFFER(buffer_name, desc, ...)                                                       \
    char tmp;                                                                                      \
    size_t req_size = _GEN_ASSERT_DESC(&tmp, 1, desc, ##__VA_ARGS__) + 1;                          \
    char* buffer_name __attribute__((cleanup(zxtest_clean_buffer))) =                              \
        (char*)malloc(req_size * sizeof(char));                                                    \
    memset(buffer_name, '\0', req_size);

#define _ASSERT_VAR_BYTES(op, expected, actual, size, fatal, file, line, desc, ...)                \
    do {                                                                                           \
        const void* _actual = (actual);                                                            \
        const void* _expected = (expected);                                                        \
        if (!(op(_actual, _expected, size))) {                                                     \
            _LOAD_BUFFER(msg_buffer, desc, __VA_ARGS__);                                           \
            _GEN_ASSERT_DESC(msg_buffer, req_size, desc, ##__VA_ARGS__);                           \
            _ZXTEST_LOAD_PRINT_HEX(_actual, size, act, line);                                      \
            _ZXTEST_LOAD_PRINT_HEX(_expected, size, exptd, line);                                  \
            _ZXTEST_ASSERT(msg_buffer, #expected, _ZXTEST_GET_PRINT_VAR(_expected, exptd, line),   \
                           #actual, _ZXTEST_GET_PRINT_VAR(_actual, act, line), file, line, fatal); \
            if (fatal && _ZXTEST_ABORT_IF_ERROR) {                                                 \
                return;                                                                            \
            }                                                                                      \
        }                                                                                          \
    } while (0)

#define _ASSERT_VAR(op, expected, actual, fatal, file, line, desc, ...)                            \
    do {                                                                                           \
        const _ZXTEST_AUTO_VAR_TYPE(actual) _actual = (actual);                                    \
        const _ZXTEST_AUTO_VAR_TYPE(expected) _expected = (expected);                              \
        if (!(op(_actual, _expected))) {                                                           \
            _LOAD_BUFFER(msg_buffer, desc, __VA_ARGS__);                                           \
            _GEN_ASSERT_DESC(msg_buffer, req_size, desc, ##__VA_ARGS__);                           \
            _ZXTEST_LOAD_PRINT_VAR(_actual, act, line);                                            \
            _ZXTEST_LOAD_PRINT_VAR(_expected, exptd, line);                                        \
            _ZXTEST_ASSERT(msg_buffer, #expected, _ZXTEST_GET_PRINT_VAR(_expected, exptd, line),   \
                           #actual, _ZXTEST_GET_PRINT_VAR(_actual, act, line), file, line, fatal); \
            if (fatal && _ZXTEST_ABORT_IF_ERROR) {                                                 \
                return;                                                                            \
            }                                                                                      \
        }                                                                                          \
    } while (0)

#define _ASSERT_PTR(op, expected, actual, fatal, file, line, desc, ...)                            \
    do {                                                                                           \
        const void* _actual = (const void*)(actual);                                               \
        const void* _expected = (const void*)(expected);                                           \
        if (!(op(_actual, _expected))) {                                                           \
            _LOAD_BUFFER(msg_buffer, desc, __VA_ARGS__);                                           \
            _GEN_ASSERT_DESC(msg_buffer, req_size, desc, ##__VA_ARGS__);                           \
            _ZXTEST_LOAD_PRINT_VAR(_actual, act, line);                                            \
            _ZXTEST_LOAD_PRINT_VAR(_expected, exptd, line);                                        \
            _ZXTEST_ASSERT(msg_buffer, #expected, _ZXTEST_GET_PRINT_VAR(_expected, exptd, line),   \
                           #actual, _ZXTEST_GET_PRINT_VAR(_actual, act, line), file, line, fatal); \
            if (fatal && _ZXTEST_ABORT_IF_ERROR) {                                                 \
                return;                                                                            \
            }                                                                                      \
        }                                                                                          \
    } while (0)

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
    _ASSERT_VAR(_STREQ, val2, val1, true, __FILE__, __LINE__,                                      \
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
    _ASSERT_VAR(_LE, val1, ZX_OK, true, __FILE__, __LINE__, "Expected " #val1 " is ZX_OK.",        \
                ##__VA_ARGS__)

#define EXPECT_OK(val1, ...)                                                                       \
    _ASSERT_VAR(_LE, val1, ZX_OK, false, __FILE__, __LINE__, "Expected " #val1 " is ZX_OK.",       \
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
    _ASSERT_VAR(_EQ, val, true, true, __FILE__, __LINE__, "Expected " #val " is true.",            \
                ##__VA_ARGS__)

#define ASSERT_FALSE(val, ...)                                                                     \
    _ASSERT_VAR(_EQ, val, false, true, __FILE__, __LINE__, "Expected " #val " is false.",          \
                ##__VA_ARGS__)

#define EXPECT_TRUE(val, ...)                                                                      \
    _ASSERT_VAR(_EQ, val, true, false, __FILE__, __LINE__, "Expected " #val " is true.",           \
                ##__VA_ARGS__)

#define EXPECT_FALSE(val, ...)                                                                     \
    _ASSERT_VAR(_EQ, val, false, false, __FILE__, __LINE__, "Expected " #val " is false.",         \
                ##__VA_ARGS__)

#define FAIL(...)                                                                                  \
    _ASSERT_VAR(_EQ, false, true, true, __FILE__, __LINE__, "Failure condition met.", ##__VA_ARGS__)

#define ASSERT_NO_FATAL_FAILURES(...)                                                              \
    _ASSERT_VAR(_EQ, false, true, true, __FILE__, __LINE__, "Test registered fatal failures.",     \
                ##__VA_ARGS__)
