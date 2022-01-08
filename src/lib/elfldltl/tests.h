// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_TESTS_H_
#define SRC_LIB_ELFLDLTL_TESTS_H_

#include <lib/elfldltl/layout.h>

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

#endif  // SRC_LIB_ELFLDLTL_TESTS_H_
