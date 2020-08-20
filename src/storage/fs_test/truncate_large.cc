// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <tuple>
#include <vector>

#include "src/storage/fs_test/truncate_fixture.h"

namespace fs_test {
namespace {

std::vector<LargeTruncateTestParamType> GetTestCombinations(
    const std::vector<std::tuple</*buf_size=*/size_t, /*iterations=*/size_t,
                                 LargeTruncateTestType>>& variations) {
  std::vector<LargeTruncateTestParamType> test_combinations;
  for (TestFilesystemOptions options : AllTestFilesystems()) {
    for (const auto& variation : variations) {
      if (std::get<2>(variation) == LargeTruncateTestType::Remount &&
          !options.filesystem->GetTraits().can_unmount) {
        continue;
      }
      options.device_block_count = 3 * (1LLU << 16);
      options.device_block_size = 1LLU << 9;
      options.fvm_slice_size = 1LLU << 23;
      test_combinations.push_back(std::make_tuple(options, variation));
    }
  }
  return test_combinations;
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, LargeTruncateTest,
                         testing::ValuesIn(GetTestCombinations(
                             {std::make_tuple(1 << 20, 50, LargeTruncateTestType::KeepOpen),
                              std::make_tuple(1 << 20, 50, LargeTruncateTestType::Reopen),
                              std::make_tuple(1 << 20, 50, LargeTruncateTestType::Remount),
                              std::make_tuple(1 << 25, 50, LargeTruncateTestType::KeepOpen),
                              std::make_tuple(1 << 25, 50, LargeTruncateTestType::Reopen),
                              std::make_tuple(1 << 25, 50, LargeTruncateTestType::Remount)})),
                         GetDescriptionForLargeTruncateTestParamType);

}  // namespace
}  // namespace fs_test
