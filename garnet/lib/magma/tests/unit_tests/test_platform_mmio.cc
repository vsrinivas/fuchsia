// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "mock/mock_mmio.h"

static void test_mock_mmio(magma::PlatformMmio* mmio) {
  ASSERT_NE(mmio, nullptr);

  // Verify we can write to and read from the mmio space.
  {
    uint32_t expected = 0xdeadbeef;
    mmio->Write32(0, expected);
    uint32_t val = mmio->Read32(0);
    EXPECT_EQ(val, expected);

    mmio->Write32(mmio->size() - sizeof(uint32_t), expected);
    val = mmio->Read32(mmio->size() - sizeof(uint32_t));
    EXPECT_EQ(val, expected);
  }

  {
    uint64_t expected = 0xabcddeadbeef1234;
    mmio->Write64(0, expected);
    uint64_t val = mmio->Read64(0);
    EXPECT_EQ(val, expected);

    mmio->Write64(mmio->size() - sizeof(uint64_t), expected);
    val = mmio->Read64(mmio->size() - sizeof(uint64_t));
    EXPECT_EQ(val, expected);
  }
}

TEST(PlatformMmio, MockMmio) {
  test_mock_mmio(std::unique_ptr<MockMmio>(MockMmio::Create(8)).get());
  test_mock_mmio(std::unique_ptr<MockMmio>(MockMmio::Create(16)).get());
  test_mock_mmio(std::unique_ptr<MockMmio>(MockMmio::Create(64)).get());
  test_mock_mmio(std::unique_ptr<MockMmio>(MockMmio::Create(1024)).get());
}
