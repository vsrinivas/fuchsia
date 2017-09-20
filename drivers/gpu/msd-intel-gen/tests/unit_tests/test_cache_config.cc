// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cache_config.h"
#include "instructions.h"
#include "msd_intel_buffer.h"
#include "registers.h"
#include "gtest/gtest.h"

using namespace registers;

class TestCacheConfig {
public:
    class Writer : public InstructionWriter {
    public:
        Writer(uint32_t* ptr) : ptr_(ptr) {}

        void write_dword(uint32_t dword) override { *ptr_++ = dword; }

    private:
        uint32_t* ptr_;
    };

    static void Test()
    {
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
        DLOG("0x%x", ptr[0]);
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
            const uint32_t kOffset =
                MemoryObjectControlState::kGraphicsOffset + i * sizeof(uint32_t);
            DLOG("0x%x: 0x%08x", ptr[0], ptr[1]);
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

        DLOG("0x%x", ptr[0]);
        EXPECT_EQ((uint32_t)MiNoop::kCommandType, *ptr++);

        constexpr uint32_t kLncfLoadRegisterImmediate =
            MiLoadDataImmediate::kCommandType |
            (CacheConfig::kLncfMemoryObjectControlStateEntries * 2 + 1 - 2);

        DLOG("0x%x", ptr[0]);
        EXPECT_EQ(kLncfLoadRegisterImmediate, *ptr++);

        constexpr uint32_t kIndexZero = (LncfMemoryObjectControlState::WRITEBACK
                                         << LncfMemoryObjectControlState::kCacheabilityShift)
                                            << 16 |
                                        (LncfMemoryObjectControlState::UNCACHED
                                         << LncfMemoryObjectControlState::kCacheabilityShift);

        constexpr uint32_t kIndexOne = (LncfMemoryObjectControlState::UNCACHED
                                        << LncfMemoryObjectControlState::kCacheabilityShift)
                                           << 16 |
                                       (LncfMemoryObjectControlState::WRITEBACK
                                        << LncfMemoryObjectControlState::kCacheabilityShift);

        constexpr uint32_t kIndexOther = (LncfMemoryObjectControlState::UNCACHED
                                          << LncfMemoryObjectControlState::kCacheabilityShift)
                                             << 16 |
                                         (LncfMemoryObjectControlState::UNCACHED
                                          << LncfMemoryObjectControlState::kCacheabilityShift);

        for (uint32_t i = 0; i < CacheConfig::kLncfMemoryObjectControlStateEntries; i++) {
            const uint32_t kOffset = LncfMemoryObjectControlState::kOffset + i * sizeof(uint32_t);
            DLOG("0x%x: 0x%08x", ptr[0], ptr[1]);
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

        DLOG("0x%x", ptr[0]);
        EXPECT_EQ((uint32_t)MiNoop::kCommandType, *ptr++);
    }
};

TEST(CacheConfig, Test) { TestCacheConfig::Test(); }
