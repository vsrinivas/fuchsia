// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fzl/mapped-vmo.h>

#include <limits.h>
#include <stddef.h>
#include <unittest/unittest.h>
#include <zircon/syscalls.h>

namespace {

constexpr size_t page = PAGE_SIZE;
constexpr char vmo_name[ZX_MAX_NAME_LEN] = "my-vmo";

bool CreateTest() {
    BEGIN_TEST;

    fbl::unique_ptr<fzl::MappedVmo> mapped_vmo;
    zx_status_t status = fzl::MappedVmo::Create(page, vmo_name, &mapped_vmo);

    EXPECT_EQ(status, ZX_OK);
    EXPECT_NONNULL(mapped_vmo);
    EXPECT_NE(mapped_vmo->GetVmo(), ZX_HANDLE_INVALID);
    EXPECT_EQ(mapped_vmo->GetSize(), page);
    EXPECT_NONNULL(mapped_vmo->GetData());

    auto data = static_cast<const uint8_t*>(mapped_vmo->GetData());
    for (size_t i = 0; i < page; ++i) {
        EXPECT_EQ(data[i], 0);
    }

    auto vmo = mapped_vmo->GetVmo();
    char name[ZX_MAX_NAME_LEN] = {};
    status = zx_object_get_property(vmo, ZX_PROP_NAME, name, ZX_MAX_NAME_LEN);
    EXPECT_EQ(status, ZX_OK);
    for (size_t i = 0; i < ZX_MAX_NAME_LEN; ++i) {
        EXPECT_EQ(name[i], vmo_name[i]);
    }

    END_TEST;
}

bool ReadTest() {
    BEGIN_TEST;

    fbl::unique_ptr<fzl::MappedVmo> mapped_vmo;
    zx_status_t status = fzl::MappedVmo::Create(page, vmo_name, &mapped_vmo);
    EXPECT_EQ(status, ZX_OK);

    uint8_t bytes[page];
    memset(bytes, 0xff, page);

    status = zx_vmo_read(mapped_vmo->GetVmo(), bytes, 0, page);
    EXPECT_EQ(status, ZX_OK);
    for (size_t i = 0; i < page; ++i) {
        EXPECT_EQ(bytes[i], 0);
    }

    END_TEST;
}

// Test that touching memory, then zx_vmo_reading, works as expected.
bool WriteMappingTest() {
    BEGIN_TEST;

    fbl::unique_ptr<fzl::MappedVmo> mapped_vmo;
    zx_status_t status = fzl::MappedVmo::Create(page, vmo_name, &mapped_vmo);
    EXPECT_EQ(status, ZX_OK);

    auto data = static_cast<uint8_t*>(mapped_vmo->GetData());
    memset(data, 0xff, page);

    uint8_t bytes[page] = {};
    status = zx_vmo_read(mapped_vmo->GetVmo(), bytes, 0, page);
    EXPECT_EQ(status, ZX_OK);
    for (size_t i = 0; i < page; ++i) {
        EXPECT_EQ(bytes[i], 0xff);
    }

    END_TEST;
}

// Test that zx_vmo_writing, then reading memory, works as expected.
bool ReadMappingTest() {
    BEGIN_TEST;

    fbl::unique_ptr<fzl::MappedVmo> mapped_vmo;
    zx_status_t status = fzl::MappedVmo::Create(page, vmo_name, &mapped_vmo);
    EXPECT_EQ(status, ZX_OK);

    uint8_t bytes[page];
    memset(bytes, 0xff, page);
    status = zx_vmo_write(mapped_vmo->GetVmo(), bytes, 0, page);
    EXPECT_EQ(status, ZX_OK);

    auto data = static_cast<uint8_t*>(mapped_vmo->GetData());
    for (size_t i = 0; i < page; ++i) {
        EXPECT_EQ(data[i], 0xff);
    }

    END_TEST;
}

bool EmptyNameTest() {
    BEGIN_TEST;

    fbl::unique_ptr<fzl::MappedVmo> mapped_vmo;
    zx_status_t status = fzl::MappedVmo::Create(page, "", &mapped_vmo);
    EXPECT_EQ(status, ZX_OK);

    auto vmo = mapped_vmo->GetVmo();
    char name[ZX_MAX_NAME_LEN] = {};
    status = zx_object_get_property(vmo, ZX_PROP_NAME, name, ZX_MAX_NAME_LEN);
    EXPECT_EQ(status, ZX_OK);
    for (size_t i = 0; i < ZX_MAX_NAME_LEN; ++i) {
        EXPECT_EQ(name[i], 0);
    }

    END_TEST;
}

bool NullptrNameTest() {
    BEGIN_TEST;

    fbl::unique_ptr<fzl::MappedVmo> mapped_vmo;
    zx_status_t status = fzl::MappedVmo::Create(page, nullptr, &mapped_vmo);
    EXPECT_EQ(status, ZX_OK);

    auto vmo = mapped_vmo->GetVmo();
    char name[ZX_MAX_NAME_LEN] = {};
    status = zx_object_get_property(vmo, ZX_PROP_NAME, name, ZX_MAX_NAME_LEN);
    EXPECT_EQ(status, ZX_OK);
    for (size_t i = 0; i < ZX_MAX_NAME_LEN; ++i) {
        EXPECT_EQ(name[i], 0);
    }

    END_TEST;
}

bool LongNameTest() {
    BEGIN_TEST;

    char long_name[page];
    memset(long_name, 'x', page);
    long_name[page - 1] = 0;

    fbl::unique_ptr<fzl::MappedVmo> mapped_vmo;
    zx_status_t status = fzl::MappedVmo::Create(page, long_name, &mapped_vmo);
    EXPECT_EQ(status, ZX_OK);

    auto vmo = mapped_vmo->GetVmo();
    char name[ZX_MAX_NAME_LEN] = {};
    status = zx_object_get_property(vmo, ZX_PROP_NAME, name, ZX_MAX_NAME_LEN);
    EXPECT_EQ(status, ZX_OK);
    for (size_t i = 0; i < ZX_MAX_NAME_LEN - 1; ++i) {
        EXPECT_EQ(name[i], 'x');
    }
    EXPECT_EQ(name[ZX_MAX_NAME_LEN - 1], 0);

    END_TEST;
}

bool GoodSizesTest() {
    BEGIN_TEST;

    size_t sizes[] = {
        page,
        16 * page,
        page * page,
        page + 1,
    };

    for (size_t size : sizes) {
        fbl::unique_ptr<fzl::MappedVmo> mapped_vmo;
        zx_status_t status = fzl::MappedVmo::Create(size, vmo_name, &mapped_vmo);
        EXPECT_EQ(status, ZX_OK);

        auto data = static_cast<const uint8_t*>(mapped_vmo->GetData());
        for (size_t i = 0; i < size; ++i) {
            EXPECT_EQ(data[i], 0);
        }
    }

    END_TEST;
}

bool BadSizesTest() {
    BEGIN_TEST;

    // Size 0 should fail.
    fbl::unique_ptr<fzl::MappedVmo> mapped_vmo;
    zx_status_t status = fzl::MappedVmo::Create(0, vmo_name, &mapped_vmo);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_NULL(mapped_vmo);

    // So should an aburdly big request.
    status = fzl::MappedVmo::Create(SIZE_MAX, vmo_name, &mapped_vmo);
    EXPECT_EQ(status, ZX_ERR_OUT_OF_RANGE);
    EXPECT_NULL(mapped_vmo);

    END_TEST;
}

bool GoodShrinkTest() {
    BEGIN_TEST;

    size_t size = page * page;

    fbl::unique_ptr<fzl::MappedVmo> mapped_vmo;
    zx_status_t status = fzl::MappedVmo::Create(size, vmo_name, &mapped_vmo);
    EXPECT_EQ(status, ZX_OK);

    while (size > 2 * page) {
        // The current size.
        status = mapped_vmo->Shrink(mapped_vmo->GetSize());
        EXPECT_EQ(status, ZX_OK);
        EXPECT_EQ(mapped_vmo->GetSize(), size);

        // A paged aligned size.
        size >>= 1;
        status = mapped_vmo->Shrink(size);
        EXPECT_EQ(status, ZX_OK);
        EXPECT_EQ(mapped_vmo->GetSize(), size);
    }

    // TODO: Test that shrinking the map causes subsequent memory
    // accesses to fail.

    END_TEST;
}

bool BadShrinkTest() {
    BEGIN_TEST;

    const size_t size = 16 * page;

    fbl::unique_ptr<fzl::MappedVmo> mapped_vmo;
    zx_status_t status = fzl::MappedVmo::Create(size, vmo_name, &mapped_vmo);
    EXPECT_EQ(status, ZX_OK);

    // Shrinking to 0 should fail.
    status = mapped_vmo->Shrink(0);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_EQ(mapped_vmo->GetSize(), size);

    // Growing via shrink should also fail.
    status = mapped_vmo->Shrink(2 * mapped_vmo->GetSize());
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_EQ(mapped_vmo->GetSize(), size);

    // Growing to a misaligned size should also fail.
    status = mapped_vmo->Shrink(page + 23);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_EQ(mapped_vmo->GetSize(), size);

    END_TEST;
}

bool AlignedGoodGrowTest() {
    BEGIN_TEST;

    const size_t original_size = page;
    const size_t grow_size = 2 * page;

    fbl::unique_ptr<fzl::MappedVmo> mapped_vmo;
    zx_status_t status = fzl::MappedVmo::Create(original_size, vmo_name, &mapped_vmo);
    EXPECT_EQ(status, ZX_OK);

    // Growing to the current size should always succeed.
    status = mapped_vmo->Grow(mapped_vmo->GetSize());
    EXPECT_EQ(status, ZX_OK);

    status = mapped_vmo->Grow(grow_size);
    if (status == ZX_OK) {
        EXPECT_EQ(mapped_vmo->GetSize(), grow_size);
        // Check the last byte.
        auto data = static_cast<const uint8_t*>(mapped_vmo->GetData());
        EXPECT_EQ(data[grow_size - 1], 0);
    } else {
        // We might just get unlucky and get a page adjacent to
        // something and not be able to grow. If so, assert that the
        // size did not change.
        EXPECT_EQ(mapped_vmo->GetSize(), original_size);
    }

    END_TEST;
}

bool UnalignedGoodGrowTest() {
    BEGIN_TEST;

    const size_t original_size = page;
    const size_t grow_size = 2 * page + 1;
    const size_t rounded_grow_size = 3 * page;

    fbl::unique_ptr<fzl::MappedVmo> mapped_vmo;
    zx_status_t status = fzl::MappedVmo::Create(original_size, vmo_name, &mapped_vmo);
    EXPECT_EQ(status, ZX_OK);

    // Growing to the current size should always succeed.
    status = mapped_vmo->Grow(mapped_vmo->GetSize());
    EXPECT_EQ(status, ZX_OK);

    status = mapped_vmo->Grow(grow_size);
    if (status == ZX_OK) {
        EXPECT_EQ(mapped_vmo->GetSize(), rounded_grow_size);
        // Check the last byte.
        auto data = static_cast<const uint8_t*>(mapped_vmo->GetData());
        EXPECT_EQ(data[grow_size - 1], 0);
    } else {
        // We might just get unlucky and get a page adjacent to
        // something and not be able to grow. If so, assert that the
        // size did not change.
        EXPECT_EQ(mapped_vmo->GetSize(), original_size);
    }

    END_TEST;
}

bool BadGrowTest() {
    BEGIN_TEST;

    const size_t original_size = 2 * page;
    const size_t grow_size = page;

    fbl::unique_ptr<fzl::MappedVmo> mapped_vmo;
    zx_status_t status = fzl::MappedVmo::Create(original_size, vmo_name, &mapped_vmo);
    EXPECT_EQ(status, ZX_OK);

    // Growing from 2 pages to 1 should fail.
    status = mapped_vmo->Grow(grow_size);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_EQ(mapped_vmo->GetSize(), original_size);

    // Growing from 2 pages to nothing should also fail.
    status = mapped_vmo->Grow(0);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_EQ(mapped_vmo->GetSize(), original_size);

    END_TEST;
}

BEGIN_TEST_CASE(MappedVmoTest)
RUN_TEST(CreateTest)
RUN_TEST(ReadTest)
RUN_TEST(WriteMappingTest)
RUN_TEST(ReadMappingTest)
RUN_TEST(EmptyNameTest)
RUN_TEST(NullptrNameTest)
RUN_TEST(LongNameTest)
RUN_TEST(GoodSizesTest)
RUN_TEST(BadSizesTest)
RUN_TEST(GoodShrinkTest)
RUN_TEST(BadShrinkTest)
RUN_TEST(AlignedGoodGrowTest)
RUN_TEST(UnalignedGoodGrowTest)
RUN_TEST(BadGrowTest)
END_TEST_CASE(MappedVmoTest)

} // namespace

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
