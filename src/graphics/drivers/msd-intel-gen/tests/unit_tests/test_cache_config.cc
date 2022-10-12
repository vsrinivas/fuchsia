// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "cache_config.h"
#include "instructions.h"
#include "mock/mock_mmio.h"
#include "msd_intel_buffer.h"
#include "registers.h"

using namespace registers;

class TestCacheConfig : public testing::Test {
 public:
  class Writer : public magma::InstructionWriter {
   public:
    Writer(uint32_t* ptr) : ptr_(ptr) {}

    void Write32(uint32_t dword) override { *ptr_++ = dword; }

   private:
    uint32_t* ptr_;
  };
};

TEST_F(TestCacheConfig, InitCacheConfig) {
  const uint64_t expected_size =
      (MiLoadDataImmediate::dword_count(CacheConfig::kMemoryObjectControlStateEntries) +
       MiLoadDataImmediate::dword_count(CacheConfig::kLncfMemoryObjectControlStateEntries) +
       MiNoop::kDwordCount * 2 + MiBatchBufferEnd::kDwordCount) *
      sizeof(uint32_t);
  EXPECT_EQ(expected_size, CacheConfig::InstructionBytesRequired());

  std::shared_ptr<MsdIntelBuffer> buffer = MsdIntelBuffer::Create(PAGE_SIZE, "test");

  uint32_t* ptr;
  ASSERT_TRUE(buffer->platform_buffer()->MapCpu(reinterpret_cast<void**>(&ptr)));

  Writer writer(ptr);

  EXPECT_TRUE(CacheConfig::InitCacheConfig(&writer, RENDER_COMMAND_STREAMER));

  constexpr uint32_t kMocsLoadRegisterImmediate =
      MiLoadDataImmediate::kCommandType |
      (CacheConfig::kMemoryObjectControlStateEntries * 2 + 1 - 2);
  EXPECT_EQ(kMocsLoadRegisterImmediate, *ptr++);

  constexpr uint32_t kMocsUncached =
      (MemoryObjectControlState::LRU_0 << MemoryObjectControlState::kLruManagementShift) |
      (MemoryObjectControlState::LLC_ELLC << MemoryObjectControlState::kCacheShift) |
      (MemoryObjectControlState::UNCACHED << MemoryObjectControlState::kCacheabilityShift);

  constexpr uint32_t kMocsPageTable =
      (MemoryObjectControlState::LRU_3 << MemoryObjectControlState::kLruManagementShift) |
      (MemoryObjectControlState::LLC_ELLC << MemoryObjectControlState::kCacheShift) |
      (MemoryObjectControlState::PAGETABLE << MemoryObjectControlState::kCacheabilityShift);

  constexpr uint32_t kMocsCached =
      (MemoryObjectControlState::LRU_3 << MemoryObjectControlState::kLruManagementShift) |
      (MemoryObjectControlState::LLC_ELLC << MemoryObjectControlState::kCacheShift) |
      (MemoryObjectControlState::WRITEBACK << MemoryObjectControlState::kCacheabilityShift);

  for (uint32_t i = 0; i < CacheConfig::kMemoryObjectControlStateEntries; i++) {
    const uint32_t kOffset = MemoryObjectControlState::kGraphicsOffset + i * sizeof(uint32_t);
    EXPECT_EQ(*ptr++, kOffset);
    switch (i) {
      case 1:
        EXPECT_EQ(*ptr++, kMocsPageTable);
        break;
      case 2:
        EXPECT_EQ(*ptr++, kMocsCached);
        break;
      case 0:
      default:
        EXPECT_EQ(*ptr++, kMocsUncached);
    }
  }

  EXPECT_EQ((uint32_t)MiNoop::kCommandType, *ptr++);

  constexpr uint32_t kLncfLoadRegisterImmediate =
      MiLoadDataImmediate::kCommandType |
      (CacheConfig::kLncfMemoryObjectControlStateEntries * 2 + 1 - 2);

  EXPECT_EQ(kLncfLoadRegisterImmediate, *ptr++);

  constexpr uint32_t kIndexZero =
      (LncfMemoryObjectControlState::WRITEBACK << LncfMemoryObjectControlState::kCacheabilityShift)
          << 16 |
      (LncfMemoryObjectControlState::UNCACHED << LncfMemoryObjectControlState::kCacheabilityShift);

  constexpr uint32_t kIndexOne =
      (LncfMemoryObjectControlState::UNCACHED << LncfMemoryObjectControlState::kCacheabilityShift)
          << 16 |
      (LncfMemoryObjectControlState::WRITEBACK << LncfMemoryObjectControlState::kCacheabilityShift);

  constexpr uint32_t kIndexOther =
      (LncfMemoryObjectControlState::UNCACHED << LncfMemoryObjectControlState::kCacheabilityShift)
          << 16 |
      (LncfMemoryObjectControlState::UNCACHED << LncfMemoryObjectControlState::kCacheabilityShift);

  for (uint32_t i = 0; i < CacheConfig::kLncfMemoryObjectControlStateEntries; i++) {
    const uint32_t kOffset = LncfMemoryObjectControlState::kOffset + i * sizeof(uint32_t);
    EXPECT_EQ(*ptr++, kOffset);
    switch (i) {
      case 0:
        EXPECT_EQ(*ptr++, kIndexZero);
        break;
      case 1:
        EXPECT_EQ(*ptr++, kIndexOne);
        break;
      default:
        EXPECT_EQ(*ptr++, kIndexOther);
    }
  }

  EXPECT_EQ((uint32_t)MiNoop::kCommandType, *ptr++);
}

TEST_F(TestCacheConfig, InitCacheConfigGen12) {
  auto register_io = std::make_unique<MsdIntelRegisterIo>(MockMmio::Create(8ULL * 1024 * 1024));

  ASSERT_TRUE(CacheConfig::InitCacheConfigGen12(register_io.get()));

  constexpr uint32_t kUncached = MemoryObjectControlState::format(
      MemoryObjectControlState::UNCACHED, MemoryObjectControlState::LLC,
      MemoryObjectControlState::LRU_0);
  constexpr uint32_t kCached = MemoryObjectControlState::format(MemoryObjectControlState::WRITEBACK,
                                                                MemoryObjectControlState::LLC,
                                                                MemoryObjectControlState::LRU_3);

  for (uint32_t i = 0; i < CacheConfig::kMemoryObjectControlStateEntries; i++) {
    uint32_t value = register_io->Read32(registers::MemoryObjectControlState::kGlobalOffsetGen12 +
                                         i * sizeof(uint32_t));
    switch (i) {
      case 2:
      case 48:
      case 60:
        EXPECT_EQ(kCached, value);
        break;
      case 3:
      default:
        EXPECT_EQ(kUncached, value);
        break;
    }
  }

  for (uint32_t i = 0; i < CacheConfig::kLncfMemoryObjectControlStateEntries * 2; i++) {
    uint32_t value32 =
        register_io->Read32(LncfMemoryObjectControlState::kOffset + i / 2 * sizeof(uint32_t));

    uint16_t value = (i % 2 == 0) ? (value32 & 0xFFFF) : value32 >> 16;

    switch (i) {
      case 2:
      case 48:
        EXPECT_EQ(value,
                  LncfMemoryObjectControlState::format(LncfMemoryObjectControlState::WRITEBACK));
        break;
      case 3:
      case 60:
      default:
        EXPECT_EQ(value,
                  LncfMemoryObjectControlState::format(LncfMemoryObjectControlState::UNCACHED));
        break;
    }
  }
}
