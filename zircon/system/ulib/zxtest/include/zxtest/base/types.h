// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZXTEST_BASE_TYPES_H_
#define ZXTEST_BASE_TYPES_H_

#include <cstdint>
#include <memory>

#include <fbl/function.h>

namespace zxtest {

// Forward Declaration.
class Test;

// Describes a location within a source file. Useful for assertions, and
// error reporting.
struct SourceLocation {
  const char* filename = nullptr;
  int64_t line_number = -1;
};

namespace internal {
// Forward Declaration.
class TestDriver;

// Function for instantiating a TestInstance.
using TestFactory = fbl::Function<std::unique_ptr<Test>(TestDriver*)>;

// Function that definest a SetUp for a TestCase.
using SetUpTestCaseFn = fbl::Function<void()>;

// Function that definest a TearDown for a TestCase.
using TearDownTestCaseFn = fbl::Function<void()>;

}  // namespace internal
}  // namespace zxtest

#endif  // ZXTEST_BASE_TYPES_H_
