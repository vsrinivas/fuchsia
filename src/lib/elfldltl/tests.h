// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_TESTS_H_
#define SRC_LIB_ELFLDLTL_TESTS_H_

#include <lib/elfldltl/diagnostics.h>
#include <lib/elfldltl/layout.h>

#include <functional>

#include <zxtest/zxtest.h>

template <class... Elf>
struct TestAllFormatsHelper {
  template <typename Test>
  void OneTest(Test&& test) const {
    ASSERT_NO_FATAL_FAILURE((test(Elf{}), ...));
  }

  template <typename... Test>
  void operator()(Test&&... tests) const {
    ASSERT_NO_FATAL_FAILURE((OneTest(tests), ...));
  }
};

template <typename... Test>
inline void TestAllFormats(Test&&... test) {
  elfldltl::AllFormats<TestAllFormatsHelper>{}(std::forward<Test>(test)...);
}

// This helper object is instantiated with the expected error string and its
// diag() method returns a Diagnostics object.  When the helper object goes out
// of scope, it will assert that the Diagnostics object got the expected one
// error logged.
class ExpectedSingleError {
 public:
  // Default-constructed, expect no errors.
  explicit ExpectedSingleError() = default;

  // With a string, expect exactly that one error.
  explicit ExpectedSingleError(std::string_view expected) : expected_(expected) {
    ASSERT_FALSE(expected_.empty());
  }

  auto& diag() { return diag_; }

  template <typename... Args>
  bool operator()(std::string_view error, Args&&... args) {
    EXPECT_STREQ(error, expected_, "\"%.*s\" != \"%.*s\"", static_cast<int>(error.size()),
                 error.data(), static_cast<int>(expected_.size()), expected_.data());
    expected_ = {};
    return true;
  }

  ~ExpectedSingleError() { EXPECT_TRUE(expected_.empty()); }

 private:
  // Diagnostic flags for signaling as much information as possible.
  static constexpr elfldltl::DiagnosticsFlags kFlags = {
      .multiple_errors = true,
      .warnings_are_errors = false,
      .extra_checking = true,
  };

  std::string_view expected_;
  elfldltl::Diagnostics<std::reference_wrapper<ExpectedSingleError>> diag_{*this, kFlags};
};

#endif  // SRC_LIB_ELFLDLTL_TESTS_H_
