// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest.h"

#ifndef LIB_STDCOMPAT_TEST_TEST_HELPER_H_
#define LIB_STDCOMPAT_TEST_TEST_HELPER_H_

// We have asserts to detect undefined behavior on release builds, but on non release build we want
// to fail fast.
#if !defined(NDEBUG) && defined(LIB_STDCOMPAT_USE_POLYFILLS)
#define DEBUG_ASSERT_DEATH(stmnt) ASSERT_DEATH(stmnt, ".*")
#else
#define DEBUG_ASSERT_DEATH(stmnt) GTEST_SKIP() << "Test verifies polyfill debug only behavior."
#endif

// Defines ASSERT_THROW_OR_ABORT, which translates into checking that the code throws an exception
// when exceptions are enabled, or the process aborts execution when exceptions are disabled, by
// relying on a 'DEATH_TEST'.
#if defined(__cpp_exceptions) && __cpp_exceptions >= 199711L
#define ASSERT_THROW_OR_ABORT(stmnt, error) ASSERT_THROW(stmnt, error);
#else
#define ASSERT_THROW_OR_ABORT(stmnt, error) ASSERT_DEATH(stmnt, ".*");
#endif

#endif  // LIB_STDCOMPAT_TEST_TEST_HELPER_H_
