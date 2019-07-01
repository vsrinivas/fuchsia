// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZXTEST_BASE_TEST_INFO_H_
#define ZXTEST_BASE_TEST_INFO_H_

#include <memory>

#include <fbl/string.h>
#include <zxtest/base/test-driver.h>
#include <zxtest/base/test.h>
#include <zxtest/base/types.h>

namespace zxtest {

class TestInfo {
 public:
  TestInfo() = delete;
  TestInfo(const fbl::String& name, const SourceLocation& location, internal::TestFactory factory);
  TestInfo(const TestInfo&) = delete;
  TestInfo(TestInfo&& rhs);
  TestInfo& operator=(const TestInfo&) = delete;
  TestInfo& operator=(TestInfo&&) = delete;
  ~TestInfo();

  // Returns an instance of the described test.
  std::unique_ptr<Test> Instantiate(internal::TestDriver* driver) const;

  // Returns the name used to register the test.
  const fbl::String& name() const {
    return name_;
  }

  // Returns the code location where the test was registered.
  const SourceLocation& location() const {
    return location_;
  }

 private:
  internal::TestFactory factory_ = nullptr;

  fbl::String name_;

  SourceLocation location_;
};

}  // namespace zxtest

#endif  // ZXTEST_BASE_TEST_INFO_H_
