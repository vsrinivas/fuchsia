// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"
#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"

namespace zxdb {

class MockSymbolDataProviderTest : public TestWithLoop {
 public:
  std::vector<uint8_t> SyncRead(fxl::RefPtr<MockSymbolDataProvider> provider,
                                uint64_t address, uint32_t size) {
    std::vector<uint8_t> result;
    provider->GetMemoryAsync(
        address, size,
        [this, &result](const Err& err, std::vector<uint8_t> data) {
          result = std::move(data);
          loop().QuitNow();
        });
    loop().Run();
    return result;
  }
};

TEST_F(MockSymbolDataProviderTest, MemReadRanges) {
  auto provider = fxl::MakeRefCounted<MockSymbolDataProvider>();
  const uint64_t kBegin = 0x1000;
  const uint64_t kSize = 10;

  std::vector<uint8_t> full_data = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  provider->AddMemory(kBegin, full_data);

  // Read before or after the memory should give nothing.
  auto read = SyncRead(provider, 0x10, 0x10);
  EXPECT_TRUE(read.empty());
  read = SyncRead(provider, 0x2000, 10);
  EXPECT_TRUE(read.empty());

  // Read outside the boundaries of the region.
  read = SyncRead(provider, kBegin - 10, 10);
  EXPECT_TRUE(read.empty());
  read = SyncRead(provider, kBegin + kSize, 10);
  EXPECT_TRUE(read.empty());

  // Read exactly the region.
  read = SyncRead(provider, kBegin, kSize);
  EXPECT_EQ(full_data, read);

  // Read across the beginning, this is a failure.
  read = SyncRead(provider, kBegin - 2, 4);
  EXPECT_TRUE(read.empty());

  // Read across the end, this is a short read.
  read = SyncRead(provider, kBegin + kSize - 2, 4);
  std::vector<uint8_t> across_end = {8, 9};
  EXPECT_EQ(across_end, read);

  // Read in the middle.
  read = SyncRead(provider, kBegin + 2, 4);
  std::vector<uint8_t> middle = {2, 3, 4, 5};
  EXPECT_EQ(middle, read);
}

}  // namespace zxdb
