// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <fbl/algorithm.h>
#include <fbl/string.h>
#include <fbl/string_piece.h>
#include <lib/bootfs/parser.h>
#include <lib/zx/vmo.h>
#include <unittest/unittest.h>
#include <zircon/boot/bootdata.h>

namespace {

constexpr uint64_t kVmoSize = 1024 * 1024;

struct BootfsEntry {
    fbl::String name;
    fbl::StringPiece data;
};

// helper for creating a bootfs to use
zx_status_t CreateBootfs(BootfsEntry* entries, size_t num_entries, zx::vmo* vmo_out) {
    zx::vmo vmo;
    zx_status_t status;
    status = zx::vmo::create(kVmoSize, 0, &vmo);
    if (status != ZX_OK) {
        return status;
    }

    uint32_t offset = static_cast<uint32_t>(sizeof(bootfs_header_t));
    for (size_t i = 0; i < num_entries; ++i) {
        auto& entry = entries[i];
        // Must be page-aligned
        const uint32_t data_offset = static_cast<uint32_t>(ZX_PAGE_SIZE * (i + 1));

        uint32_t entry_header[3] = {
            static_cast<uint32_t>(entry.name.size() + 1), // name_len
            static_cast<uint32_t>(entry.data.size()), // data size
            data_offset,
        };

        // Write header
        status = vmo.write(entry_header, offset, sizeof(entry_header));
        if (status != ZX_OK) {
            return status;
        }
        offset += static_cast<uint32_t>(sizeof(entry_header));

        // Write name
        status = vmo.write(entry.name.c_str(), offset, entry_header[0]);
        if (status != ZX_OK) {
            return status;
        }
        offset += entry_header[0];

        // Write data
        status = vmo.write(entry.data.data(), data_offset, entry.data.size());
        if (status != ZX_OK) {
            return status;
        }

        // Entries must be 32-bit aligned
        offset = fbl::round_up(offset, 4u);
    }

    bootfs_header_t header = {};
    header.magic = BOOTFS_MAGIC;
    header.dirsize = static_cast<uint32_t>(offset - sizeof(header));

    status = vmo.write(&header, 0, sizeof(header));
    if (status != ZX_OK) {
        return status;
    }

    *vmo_out = std::move(vmo);
    return ZX_OK;
}

bool TestParseWithoutInit() {
    BEGIN_TEST;

    bootfs::Parser parser;

    ASSERT_EQ(parser.Parse([](const bootfs_entry_t* entry) { return ZX_OK; }),
              ZX_ERR_BAD_STATE);

    END_TEST;
}

bool TestInitTwice() {
    BEGIN_TEST;

    zx::vmo vmo;
    ASSERT_EQ(CreateBootfs(nullptr, 0, &vmo), ZX_OK);

    bootfs::Parser parser;
    ASSERT_EQ(parser.Init(zx::unowned_vmo(vmo)), ZX_OK);
    ASSERT_EQ(parser.Init(zx::unowned_vmo(vmo)), ZX_ERR_BAD_STATE);

    END_TEST;
}

bool TestInitBadMagic() {
    BEGIN_TEST;

    zx::vmo vmo;
    zx_status_t status;
    status = zx::vmo::create(kVmoSize, 0, &vmo);
    if (status != ZX_OK) {
        return status;
    }

    bootfs_header_t header = {};
    header.magic = BOOTFS_MAGIC ^ 1;
    header.dirsize = 0;

    status = vmo.write(&header, 0, sizeof(header));
    if (status != ZX_OK) {
        return status;
    }

    bootfs::Parser parser;
    ASSERT_EQ(parser.Init(zx::unowned_vmo(vmo)), ZX_ERR_IO);

    END_TEST;
}

bool TestInitShortHeader() {
    BEGIN_TEST;

    zx::vmo vmo;
    zx_status_t status;
    status = zx::vmo::create(0, 0, &vmo);
    if (status != ZX_OK) {
        return status;
    }

    bootfs::Parser parser;
    ASSERT_EQ(parser.Init(zx::unowned_vmo(vmo)), ZX_ERR_OUT_OF_RANGE);

    END_TEST;
}

bool TestInitCantMap() {
    BEGIN_TEST;

    zx::vmo vmo;
    ASSERT_EQ(CreateBootfs(nullptr, 0, &vmo), ZX_OK);
    ASSERT_EQ(vmo.replace(ZX_RIGHT_READ, &vmo), ZX_OK);

    bootfs::Parser parser;
    ASSERT_EQ(parser.Init(zx::unowned_vmo(vmo)), ZX_ERR_ACCESS_DENIED);

    END_TEST;
}

bool TestParseSuccess() {
    BEGIN_TEST;

    BootfsEntry entries[] = {
        {
            .name = "file 3",
            .data = "lorem ipsum",
        },
        {
            .name = "File 1",
            .data = "",
        },
        {
            .name = "file2",
            .data = "0123456789",
        },
    };
    zx::vmo vmo;
    ASSERT_EQ(CreateBootfs(entries, fbl::count_of(entries), &vmo), ZX_OK);

    bootfs::Parser parser;
    ASSERT_EQ(parser.Init(zx::unowned_vmo(vmo)), ZX_OK);

    const bootfs_entry_t* parsed_entries[3];
    size_t seen = 0;
    EXPECT_EQ(parser.Parse([&entries, &parsed_entries, &seen](const bootfs_entry_t* entry) {
        if (seen >= fbl::count_of(entries)) {
            return ZX_ERR_BAD_STATE;
        }
        parsed_entries[seen] = entry;
        ++seen;
        return ZX_OK;
    }), ZX_OK);
    ASSERT_EQ(seen, fbl::count_of(entries));

    for (size_t i = 0; i < seen; ++i) {
        const auto& real_entry = entries[i];
        const auto& parsed_entry = parsed_entries[i];
        ASSERT_EQ(parsed_entry->name_len, real_entry.name.size() + 1);
        ASSERT_EQ(parsed_entry->data_len, real_entry.data.size());
        ASSERT_BYTES_EQ(reinterpret_cast<const uint8_t*>(parsed_entry->name),
                        reinterpret_cast<const uint8_t*>(real_entry.name.c_str()),
                        parsed_entry->name_len, "");

        uint8_t buffer[parsed_entry->data_len];
        ASSERT_EQ(vmo.read(buffer, parsed_entry->data_off, sizeof(buffer)), ZX_OK);
        ASSERT_BYTES_EQ(buffer, reinterpret_cast<const uint8_t*>(real_entry.data.data()),
                        sizeof(buffer), "");
    }

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(bootfs_tests)
RUN_TEST(TestParseWithoutInit)
RUN_TEST(TestInitTwice)
RUN_TEST(TestInitBadMagic)
RUN_TEST(TestInitShortHeader)
RUN_TEST(TestInitCantMap)
RUN_TEST(TestParseSuccess)
END_TEST_CASE(bootfs_tests)
