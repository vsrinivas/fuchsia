// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/leb.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/common/data_extractor.h"

namespace zxdb {

TEST(Leb, AppendULeb) {
  // Zero.
  std::vector<uint8_t> output;
  AppendULeb(0, &output);
  ASSERT_EQ(1u, output.size());
  EXPECT_EQ(0u, output[0]);

  // One-byte value.
  output.clear();
  AppendULeb(39u, &output);
  ASSERT_EQ(1u, output.size());
  EXPECT_EQ(39u, output[0]);

  // Long value, round trip through the decoder.
  constexpr uint64_t kBigValue = 789123456999u;
  output.clear();
  AppendULeb(kBigValue, &output);
  DataExtractor extractor(output);
  std::optional<uint64_t> read_value = extractor.ReadUleb128();
  ASSERT_TRUE(read_value);
  EXPECT_EQ(kBigValue, *read_value);
}

}  // namespace zxdb
