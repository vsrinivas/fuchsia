// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ndm-ram-driver.h"

#include <unittest/unittest.h>

namespace {

constexpr uint32_t kPageSize = 2048;
constexpr uint32_t kOobSize = 16;

// 20 blocks of 32 pages, 4 bad blocks max.
constexpr ftl::VolumeOptions kDefaultOptions = {20, 4, 32 * kPageSize, kPageSize, kOobSize, 0};

bool TrivialLifetimeTest() {
    BEGIN_TEST;
    NdmRamDriver driver({});
    END_TEST;
}

// Basic smoke tests for NdmRamDriver:

bool ReadWriteTest() {
    BEGIN_TEST;
    ASSERT_TRUE(ftl::InitModules());

    NdmRamDriver driver(kDefaultOptions);
    ASSERT_EQ(nullptr, driver.Init());

    fbl::Array<uint8_t> data(new uint8_t[kPageSize * 2], kPageSize * 2);
    fbl::Array<uint8_t> oob(new uint8_t[kOobSize * 2], kOobSize * 2);

    memset(data.get(), 0x55, data.size());
    memset(oob.get(), 0x66, oob.size());

    ASSERT_EQ(ftl::kNdmOk, driver.NandWrite(5, 2, data.get(), oob.get()));

    memset(data.get(), 0, data.size());
    memset(oob.get(), 0, oob.size());
    ASSERT_EQ(ftl::kNdmOk, driver.NandRead(5, 2, data.get(), oob.get()));

    for (uint32_t i = 0; i < data.size(); i++) {
        ASSERT_EQ(0x55, data[i]);
    }

    for (uint32_t i = 0; i < oob.size(); i++) {
        ASSERT_EQ(0x66, oob[i]);
    }
    END_TEST;
}

// Writes a fixed pattern to the desired page.
bool WritePage(NdmRamDriver* driver, uint32_t page_num) {
    BEGIN_TEST;
    fbl::Array<uint8_t> data(new uint8_t[kPageSize], kPageSize);
    fbl::Array<uint8_t> oob(new uint8_t[kOobSize], kOobSize);
    memset(data.get(), 0x55, data.size());
    memset(oob.get(), 0, oob.size());

    ASSERT_EQ(ftl::kNdmOk, driver->NandWrite(page_num, 1, data.get(), oob.get()));
    END_TEST;
}

bool IsEmptyTest() {
    BEGIN_TEST;
    ASSERT_TRUE(ftl::InitModules());

    NdmRamDriver driver(kDefaultOptions);
    ASSERT_EQ(nullptr, driver.Init());

    // Use internal driver meta-data.
    ASSERT_TRUE(driver.IsEmptyPage(0, nullptr, nullptr));

    fbl::Array<uint8_t> data(new uint8_t[kPageSize], kPageSize);
    fbl::Array<uint8_t> oob(new uint8_t[kOobSize], kOobSize);
    memset(data.get(), 0x55, data.size());
    memset(oob.get(), 0, oob.size());
    ASSERT_EQ(ftl::kNdmOk, driver.NandWrite(0, 1, data.get(), oob.get()));

    // Look at both meta-data and buffers.
    ASSERT_FALSE(driver.IsEmptyPage(0, data.get(), oob.get()));

    memset(data.get(), 0xff, data.size());
    memset(oob.get(), 0xff, oob.size());

    ASSERT_TRUE(driver.IsEmptyPage(0, data.get(), oob.get()));
    END_TEST;
}

bool EraseTest() {
    BEGIN_TEST;
    ASSERT_TRUE(ftl::InitModules());

    NdmRamDriver driver(kDefaultOptions);
    ASSERT_EQ(nullptr, driver.Init());

    ASSERT_TRUE(WritePage(&driver, 0));

    ASSERT_EQ(ftl::kNdmOk, driver.NandErase(0));
    ASSERT_TRUE(driver.IsEmptyPage(0, nullptr, nullptr));
    END_TEST;
}

bool IsBadBlockTest() {
    BEGIN_TEST;
    ASSERT_TRUE(ftl::InitModules());

    NdmRamDriver driver(kDefaultOptions);
    ASSERT_EQ(nullptr, driver.Init());

    ASSERT_EQ(ftl::kFalse, driver.IsBadBlock(0));

    ASSERT_TRUE(WritePage(&driver, 0));
    ASSERT_EQ(ftl::kTrue, driver.IsBadBlock(0));
    END_TEST;
}

bool CreateVolumeTest() {
    BEGIN_TEST;
    ASSERT_TRUE(ftl::InitModules());

    NdmRamDriver driver(kDefaultOptions);
    ASSERT_EQ(nullptr, driver.Init());
    ASSERT_EQ(nullptr, driver.Attach(nullptr));
    ASSERT_TRUE(driver.Detach());
    END_TEST;
}

bool ReAttachTest() {
    BEGIN_TEST;
    ASSERT_TRUE(ftl::InitModules());

    NdmRamDriver driver(kDefaultOptions);
    ASSERT_EQ(nullptr, driver.Init());
    ASSERT_EQ(nullptr, driver.Attach(nullptr));

    ASSERT_TRUE(WritePage(&driver, 5));

    ASSERT_TRUE(driver.Detach());
    ASSERT_EQ(nullptr, driver.Attach(nullptr));

    fbl::Array<uint8_t> data(new uint8_t[kPageSize], kPageSize);
    fbl::Array<uint8_t> oob(new uint8_t[kOobSize], kOobSize);
    ASSERT_EQ(ftl::kNdmOk, driver.NandRead(5, 1, data.get(), oob.get()));

    ASSERT_FALSE(driver.IsEmptyPage(5, data.get(), oob.get()));
    END_TEST;
}

// NdmRamDriver is supposed to inject failures periodically. This tests that it
// does.
bool WriteBadBlockTest() {
    BEGIN_TEST;
    ASSERT_TRUE(ftl::InitModules());

    NdmRamDriver driver(kDefaultOptions);
    ASSERT_EQ(nullptr, driver.Init());

    fbl::Array<uint8_t> data(new uint8_t[kPageSize], kPageSize);
    fbl::Array<uint8_t> oob(new uint8_t[kOobSize], kOobSize);

    memset(data.get(), 0, data.size());
    memset(oob.get(), 0, oob.size());

    for (int i = 0; i < kBadBlockInterval; i++) {
        ASSERT_EQ(ftl::kNdmOk, driver.NandErase(0));
    }

    ASSERT_EQ(ftl::kNdmError, driver.NandWrite(0, 1, data.get(), oob.get()));
    END_TEST;
}

// NdmRamDriver is supposed to inject failures periodically. This tests that it
// does.
bool ReadUnsafeEccTest() {
    BEGIN_TEST;
    ASSERT_TRUE(ftl::InitModules());

    NdmRamDriver driver(kDefaultOptions);
    ASSERT_EQ(nullptr, driver.Init());

    fbl::Array<uint8_t> data(new uint8_t[kPageSize], kPageSize);
    fbl::Array<uint8_t> oob(new uint8_t[kOobSize], kOobSize);

    memset(data.get(), 0, data.size());
    memset(oob.get(), 0, oob.size());
    ASSERT_EQ(ftl::kNdmOk, driver.NandWrite(0, 1, data.get(), oob.get()));

    for (int i = 0; i < kEccErrorInterval; i++) {
        ASSERT_EQ(ftl::kNdmOk, driver.NandRead(0, 1, data.get(), oob.get()));
    }

    ASSERT_EQ(ftl::kNdmUnsafeEcc, driver.NandRead(0, 1, data.get(), oob.get()));
    ASSERT_EQ(ftl::kNdmOk, driver.NandRead(0, 1, data.get(), oob.get()));
    END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(DriverTests)
RUN_TEST_SMALL(TrivialLifetimeTest)
RUN_TEST_SMALL(ReadWriteTest)
RUN_TEST_SMALL(IsEmptyTest)
RUN_TEST_SMALL(EraseTest)
RUN_TEST_SMALL(IsBadBlockTest)
RUN_TEST_SMALL(CreateVolumeTest)
RUN_TEST_SMALL(ReAttachTest)
RUN_TEST_SMALL(WriteBadBlockTest)
RUN_TEST_SMALL(ReadUnsafeEccTest)
END_TEST_CASE(DriverTests)
