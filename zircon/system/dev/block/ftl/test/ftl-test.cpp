// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ftl-shell.h"

#include <stdlib.h>
#include <time.h>

#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <unittest/unittest.h>

namespace {

constexpr uint32_t kPageSize = 4096;

// 300 blocks of 64 pages.
constexpr ftl::VolumeOptions kDefaultOptions = {300, 300 / 20, 64 * kPageSize, kPageSize, 16, 0};

bool TrivialLifetimeTest() {
    BEGIN_TEST;
    FtlShell ftl;
    ASSERT_TRUE(ftl.Init(kDefaultOptions));
    END_TEST;
}

// See ReAttachTest for a non-trivial flush test.
bool TrivialFlushTest() {
    BEGIN_TEST;
    FtlShell ftl;
    ASSERT_TRUE(ftl.Init(kDefaultOptions));
    ASSERT_EQ(ZX_OK, ftl.volume()->Flush());
    END_TEST;
}

bool IsEmptyPage(FtlShell* ftl, uint32_t page_num) {
    BEGIN_TEST;
    fbl::Array<uint8_t> buffer(new uint8_t[kPageSize], kPageSize);
    memset(buffer.get(), 0, buffer.size());

    ASSERT_EQ(ZX_OK, ftl->volume()->Read(page_num, 1, buffer.get()));

    for (uint32_t i = 0; i < buffer.size(); i++) {
        ASSERT_EQ(0xff, buffer[i]);
    }
    END_TEST;
}

bool UnmountTest() {
    BEGIN_TEST;
    FtlShell ftl;
    ASSERT_TRUE(ftl.Init(kDefaultOptions));
    ASSERT_EQ(ZX_OK, ftl.volume()->Unmount());
    END_TEST;
}

bool MountTest() {
    BEGIN_TEST;
    FtlShell ftl;
    ASSERT_TRUE(ftl.Init(kDefaultOptions));
    ASSERT_EQ(ZX_OK, ftl.volume()->Unmount());
    ASSERT_EQ(ZX_OK, ftl.volume()->Mount());

    ASSERT_TRUE(IsEmptyPage(&ftl, 10));
    END_TEST;
}

bool ReadWriteTest() {
    BEGIN_TEST;
    FtlShell ftl;
    ASSERT_TRUE(ftl.Init(kDefaultOptions));

    fbl::Array<uint8_t> buffer(new uint8_t[kPageSize * 2], kPageSize * 2);
    memset(buffer.get(), 0x55, buffer.size());

    ASSERT_EQ(ZX_OK, ftl.volume()->Write(150, 2, buffer.get()));

    memset(buffer.get(), 0, buffer.size());
    ASSERT_EQ(ZX_OK, ftl.volume()->Read(150, 2, buffer.get()));

    for (uint32_t i = 0; i < buffer.size(); i++) {
        ASSERT_EQ(0x55, buffer[i]);
    }
    END_TEST;
}

bool WritePage(FtlShell* ftl, uint32_t page_num) {
    BEGIN_TEST;
    fbl::Array<uint8_t> buffer(new uint8_t[kPageSize], kPageSize);
    memset(buffer.get(), 0x55, buffer.size());

    ASSERT_EQ(ZX_OK, ftl->volume()->Write(page_num, 1, buffer.get()));
    END_TEST;
}

bool ReAttachTest() {
    BEGIN_TEST;
    FtlShell ftl;
    ASSERT_TRUE(ftl.Init(kDefaultOptions));

    fbl::Array<uint8_t> buffer(new uint8_t[kPageSize * 2], kPageSize * 2);
    memset(buffer.get(), 0x55, buffer.size());

    ASSERT_EQ(ZX_OK, ftl.volume()->Write(150, 2, buffer.get()));

    ASSERT_TRUE(ftl.ReAttach());
    ASSERT_TRUE(IsEmptyPage(&ftl, 150));

    // Try again, this time flushing before removing the volume.
    ASSERT_EQ(ZX_OK, ftl.volume()->Write(150, 2, buffer.get()));

    ASSERT_EQ(ZX_OK, ftl.volume()->Flush());
    ASSERT_TRUE(ftl.ReAttach());

    memset(buffer.get(), 0, buffer.size());
    ASSERT_EQ(ZX_OK, ftl.volume()->Read(150, 2, buffer.get()));

    for (uint32_t i = 0; i < buffer.size(); i++) {
        ASSERT_EQ(0x55, buffer[i]);
    }
    END_TEST;
}

bool FormatTest() {
    BEGIN_TEST;
    FtlShell ftl;
    ASSERT_TRUE(ftl.Init(kDefaultOptions));

    ASSERT_TRUE(WritePage(&ftl, 10));
    ASSERT_EQ(ZX_OK, ftl.volume()->Format());

    ASSERT_TRUE(IsEmptyPage(&ftl, 10));
    END_TEST;
}

bool TrimTest() {
    BEGIN_TEST;
    FtlShell ftl;
    ASSERT_TRUE(ftl.Init(kDefaultOptions));

    ASSERT_TRUE(WritePage(&ftl, 10));
    ASSERT_EQ(ZX_OK, ftl.volume()->Trim(10, 1));

    ASSERT_TRUE(IsEmptyPage(&ftl, 10));
    END_TEST;
}

bool GarbageCollectTest() {
    BEGIN_TEST;
    FtlShell ftl;
    constexpr int kBlocks = 10;
    ASSERT_TRUE(ftl.Init({kBlocks, 1, 32 * kPageSize, kPageSize, 16, 0}));

    // Even though the device is empty, the FTL erases the blocks before use,
    // and for this API that counts as garbage collection.
    // Two reserved blocks + one that may become bad.
    for (int i = 0; i < kBlocks - 3; i++) {
        ASSERT_EQ(ZX_OK, ftl.volume()->GarbageCollect());
    }
    ASSERT_EQ(ZX_ERR_STOP, ftl.volume()->GarbageCollect());
    END_TEST;
}

bool StatsTest() {
    BEGIN_TEST;
    FtlShell ftl;
    ASSERT_TRUE(ftl.Init(kDefaultOptions));

    ftl::Volume::Stats stats;
    ASSERT_EQ(ZX_OK, ftl.volume()->GetStats(&stats));
    ASSERT_EQ(0, stats.garbage_level);
    ASSERT_EQ(0, stats.wear_count);
    ASSERT_LT(0, stats.ram_used);
    END_TEST;
}

// Test fixture.
class FtlTest {
  public:
    FtlTest() : rand_seed_(static_cast<uint32_t>(time(nullptr))) { srand(rand_seed_); }
    ~FtlTest() {
        if (!AllOk()) {
            unittest_printf_critical("rand seed: %u", rand_seed_);
        }
    }

    bool Init();

    // Goes over a single iteration of the "main" ftl test. |num_pages| is the
    // number of pages to write at the same time.
    bool SingleLoop(uint32_t num_pages);

  private:
    // Returns the value to use for when writing |page_num|.
    uint32_t GetKey(uint32_t page_num) {
        return (write_counters_[page_num] << 24) | page_num;
    }

    // Fills the page buffer with a known pattern for each page.
    void PrepareBuffer(uint32_t page_num, uint32_t num_pages);

    bool CheckVolume(uint32_t num_pages);

    // This is ugly, but at least doesn't use current_test_info->all_ok directly.
    // This is the moral equivalent of HasFatalFailure().
    bool AllOk() const { END_TEST; }

    FtlShell ftl_;
    ftl::Volume* volume_ = nullptr;
    fbl::Array<uint8_t> write_counters_;
    fbl::Array<uint32_t> page_buffer_;
    uint32_t rand_seed_;  // TODO(rvargas): replace with framework provided seed when available.
};

bool FtlTest::Init() {
    BEGIN_TEST;
    ASSERT_TRUE(ftl_.Init(kDefaultOptions));
    volume_ = ftl_.volume();
    ASSERT_EQ(ZX_OK, volume_->Unmount());

    write_counters_.reset(new uint8_t[ftl_.num_pages()], ftl_.num_pages());
    memset(write_counters_.get(), 0, write_counters_.size());
    END_TEST;
}

bool FtlTest::SingleLoop(uint32_t num_pages) {
    BEGIN_TEST;
    ASSERT_EQ(ZX_OK, volume_->Mount());

    size_t buffer_size = num_pages * ftl_.page_size() / sizeof(uint32_t);
    page_buffer_.reset(new uint32_t[buffer_size], buffer_size);
    memset(page_buffer_.get(), 0, page_buffer_.size() * sizeof(page_buffer_[0]));

    // Write pages 5 - 10.
    for (uint32_t page = 5; page < 10; page++) {
        ASSERT_EQ(ZX_OK, volume_->Write(page, 1, page_buffer_.get()));
    }

    // Mark pages 5 - 10 as unused.
    ASSERT_EQ(ZX_OK, volume_->Trim(5, 5));

    // Write every page in the volume once.
    for (uint32_t page = 0; page < ftl_.num_pages();) {
        uint32_t count = fbl::min(ftl_.num_pages() - page, num_pages);
        PrepareBuffer(page, count);

        ASSERT_EQ(ZX_OK, volume_->Write(page, count, page_buffer_.get()));
        page += count;
    }

    ASSERT_EQ(ZX_OK, volume_->Flush());
    ASSERT_TRUE(CheckVolume(num_pages));

    // Randomly rewrite half the pages in the volume.
    for (uint32_t i = 0; i < ftl_.num_pages() / 2; i++) {
        uint32_t page = static_cast<uint32_t>(rand() % ftl_.num_pages());
        PrepareBuffer(page, 1);

        ASSERT_EQ(ZX_OK, volume_->Write(page, 1, page_buffer_.get()));
    }

    ASSERT_TRUE(CheckVolume(num_pages));

    // Detach and re-add test volume without erasing the media.
    ASSERT_EQ(ZX_OK, volume_->Unmount());
    ASSERT_TRUE(ftl_.ReAttach());
    ASSERT_TRUE(CheckVolume(num_pages));

    ASSERT_EQ(ZX_OK, volume_->Unmount());
    END_TEST;
}

void FtlTest::PrepareBuffer(uint32_t page_num, uint32_t num_pages) {
    uint32_t* key_buffer = page_buffer_.get();

    for (; num_pages; num_pages--, page_num++) {
        write_counters_[page_num]++;
        uint32_t value = GetKey(page_num);

        // Fill page buffer with repetitions of its unique write value.
        for (uint32_t i = 0; i < ftl_.page_size() / sizeof(value); i++) {
            *key_buffer++ = value;
        }
    }
}

bool FtlTest::CheckVolume(uint32_t num_pages) {
    BEGIN_TEST;
    for (uint32_t page = 0; page < ftl_.num_pages();) {
        uint32_t count = fbl::min(ftl_.num_pages() - page, num_pages);
        ASSERT_EQ(ZX_OK, volume_->Read(page, count, page_buffer_.get()));

        // Verify each page independently.
        uint32_t* key_buffer = page_buffer_.get();
        uint32_t* end = key_buffer + ftl_.page_size() / sizeof(uint32_t) * count;
        for (; key_buffer < end; page++) {
            // Get 32-bit data unique to most recent page write.
            uint32_t value = GetKey(page);
            for (size_t i = 0; i < ftl_.page_size(); i += sizeof(value), key_buffer++) {
                if (*key_buffer != value) {
                    unittest_printf_critical("Page #%u corrupted at offset %zu. Expected 0x%08X, "
                                             "found 0x%08X\n", page, i, value, *key_buffer);
                    ASSERT_TRUE(false);
                }
            }
        }
    }
    END_TEST;
}

bool SinglePassTest() {
    BEGIN_TEST;
    FtlTest test;
    ASSERT_TRUE(test.Init());

    ASSERT_TRUE(test.SingleLoop(5));
    END_TEST;
}

bool MultiplePassTest() {
    BEGIN_TEST;
    FtlTest test;
    ASSERT_TRUE(test.Init());

    for (int i = 1; i < 7; i++) {
        ASSERT_TRUE(test.SingleLoop(i * 3));
    }
    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(FtlTests)
RUN_TEST_SMALL(TrivialLifetimeTest)
RUN_TEST_SMALL(TrivialFlushTest)
RUN_TEST_SMALL(UnmountTest)
RUN_TEST_SMALL(MountTest)
RUN_TEST_SMALL(ReadWriteTest)
RUN_TEST_SMALL(ReAttachTest)
RUN_TEST_SMALL(FormatTest)
RUN_TEST_SMALL(TrimTest)
RUN_TEST_SMALL(GarbageCollectTest)
RUN_TEST_SMALL(StatsTest)
RUN_TEST_SMALL(SinglePassTest)
RUN_TEST_MEDIUM(MultiplePassTest)
END_TEST_CASE(FtlTests)
