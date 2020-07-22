// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FS_TEST_TRUNCATE_FIXTURE_H_
#define SRC_STORAGE_FS_TEST_TRUNCATE_FIXTURE_H_

#include <stddef.h>

#include <string>
#include <tuple>

#include "src/storage/fs_test/fs_test_fixture.h"

namespace fs_test {
struct TestFilesystemOptions;

enum class LargeTruncateTestType {
  KeepOpen,
  Reopen,
  Remount,
};

using LargeTruncateTestParamType =
    std::tuple<TestFilesystemOptions,
               std::tuple</*buffer_size=*/size_t, /*iterations=*/size_t, LargeTruncateTestType>>;

// Tests for this class are instantiated in separate files
class LargeTruncateTest : public BaseFilesystemTest,
                          public testing::WithParamInterface<LargeTruncateTestParamType> {
 public:
  LargeTruncateTest() : BaseFilesystemTest(std::get<0>(GetParam())) {}

  size_t buffer_size() const { return std::get<0>(std::get<1>(GetParam())); }
  size_t iterations() const { return std::get<1>(std::get<1>(GetParam())); }
  LargeTruncateTestType test_type() const { return std::get<2>(std::get<1>(GetParam())); }
};

std::string GetDescriptionForLargeTruncateTestParamType(
    const testing::TestParamInfo<LargeTruncateTestParamType>);

}  // namespace fs_test

#endif  // SRC_STORAGE_FS_TEST_TRUNCATE_FIXTURE_H_
