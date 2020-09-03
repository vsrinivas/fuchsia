// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FS_TEST_FS_TEST_FIXTURE_H_
#define SRC_STORAGE_FS_TEST_FS_TEST_FIXTURE_H_

#include <zircon/compiler.h>

#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "src/storage/fs_test/fs_test.h"

namespace fs_test {

class BaseFilesystemTest : public testing::Test {
 public:
  BaseFilesystemTest(const TestFilesystemOptions& options)
      : fs_(TestFilesystem::Create(options).value()) {}
  ~BaseFilesystemTest();

  std::string GetPath(std::string_view relative_path) const {
    std::string path = fs_.mount_path();
    path.append(relative_path);
    return path;
  }

  TestFilesystem& fs() { return fs_; }
  const TestFilesystem& fs() const { return fs_; }

 protected:
  TestFilesystem fs_;
};

// Example:
//
// #include "fs_test_fixture.h"
//
// using MyTest = FilesystemTest;
//
// TEST_P(MyTest, CheckThatFooSucceeds) {
//   ...
// }
//
// INSTANTIATE_TEST_SUITE_P(FooTests, MyTest,
//                          testing::ValuesIn(AllTestFilesystems()),
//                          testing::PrintToStringParamName());

class FilesystemTest : public BaseFilesystemTest,
                       public testing::WithParamInterface<TestFilesystemOptions> {
 protected:
  FilesystemTest() : BaseFilesystemTest(GetParam()) {}
};

}  // namespace fs_test

#endif  // SRC_STORAGE_FS_TEST_FS_TEST_FIXTURE_H_
