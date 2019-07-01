// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZXTEST_BASE_TEST_INTERNAL_H_
#define ZXTEST_BASE_TEST_INTERNAL_H_

#include <zxtest/base/test-driver.h>

namespace zxtest {
namespace internal {

// This is class provides an internal representation of |zxtest::Test|, where
// objects needed by the library are added. Preventing the test writer from
// accessing them.
class TestInternal {
 public:
  virtual ~TestInternal() = default;

 protected:
  TestDriver* driver_ = nullptr;
};

}  // namespace internal
}  // namespace zxtest

#endif  // ZXTEST_BASE_TEST_INTERNAL_H_
