// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

#pragma once

#include <fbl/function.h>
#include <fs-test-utils/fixture.h>
#include <unittest/unittest.h>
#include <zircon/errors.h>

// This library provides equivalent macros from unittest to generate tests that
// will use a fixture. Also provides an alternative, that provides the running
// test a pointer to the Fixture. Interaction with the underlying devices,
// such as mount/remount, etc should always be done through the Fixture class.
//
// This set of macros are compatible with RUN_TEST_* the fixture will not be passed, nor
// will SetUp/TearDown be called for those, but it is required to use BEGIN_FS_TEST_CASE*
// if at least one test of the test case requires a fixture.
//
// SetUpTestCase/TearDownTestCase will run ONCE per TestCase.
// SetUp/TearDown will run ONCE per Test run with RUN_FS_TEST_F
namespace fs_test_utils {

#define ASSERT_OK(status) ASSERT_EQ(status, ZX_OK)
#define EXPECT_OK(status) EXPECT_EQ(status, ZX_OK)

#define BEGIN_FS_TEST_CASE(name, options_fn)              \
    BEGIN_TEST_CASE(name##_##options_fn)                  \
    fs_test_utils::FixtureOptions options = options_fn(); \
    fs_test_utils::Fixture fixture(options);              \
    zx_status_t result = fixture.SetUpTestCase();

#define RUN_FS_TEST_F_TYPE(test_fn, size)                                                 \
    if (result == ZX_OK) {                                                                \
        fbl::Function<bool()> test_wrapper = [&fixture]() {                               \
            return test_fn(&fixture);                                                     \
        };                                                                                \
        fs_test_utils::fs_test_utils_internal::current_test_wrapper = &test_wrapper;      \
                                                                                          \
        result = fixture.SetUp();                                                         \
        if (result == ZX_OK) {                                                            \
            RUN_NAMED_TEST_TYPE(#test_fn,                                                 \
                                fs_test_utils::fs_test_utils_internal::RunTestWrapper,    \
                                size, false)                                              \
        } else {                                                                          \
            LOG_ERROR(result, "SetUp had errors.\n");                                     \
            RUN_NAMED_TEST_TYPE(#test_fn,                                                 \
                                fs_test_utils::fs_test_utils_internal::Fail, size, false) \
        }                                                                                 \
        result = fixture.TearDown();                                                      \
        if (result != ZX_OK) {                                                            \
            LOG_ERROR(result, "TearDown had errors.\n");                                  \
            all_success = false;                                                          \
        }                                                                                 \
    } else {                                                                              \
        LOG_ERROR(result, "SetUpTestCase had errors.\n");                                 \
        RUN_NAMED_TEST_TYPE(#test_fn,                                                     \
                            fs_test_utils::fs_test_utils_internal::Fail, size, false)     \
    }

#define END_FS_TEST_CASE(name, options_fn)                \
    result = fixture.TearDownTestCase();                  \
    if (result != ZX_OK) {                                \
        LOG_ERROR(result, "TestCaseSetUp had errors.\n"); \
        all_success = false;                              \
    }                                                     \
    END_TEST_CASE(name##_##options_fn)

#define RUN_FS_TEST_F(test_fn) RUN_FS_TEST_F_TYPE(test_fn, TEST_MEDIUM)

namespace fs_test_utils_internal {

// Pointer to current functor that will be run when RunTestWrapper is called.
static fbl::Function<bool()>* current_test_wrapper = nullptr;

// Provides a function pointer wrapper over a Callable, to allow
inline bool RunTestWrapper() {
    return (*current_test_wrapper)();
}

// Function used to make the underlying framework think the test failed,
// when setup fails, so test will not run, and will get the test listed.
inline bool Fail() {
    BEGIN_TEST;
    ASSERT_TRUE(false);
    END_TEST;
}

}; // namespace fs_test_utils_internal

} // namespace fs_test_utils
