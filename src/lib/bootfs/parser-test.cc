// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/bootfs/parser.h"

#include <lib/zx/vmo.h>
#include <zircon/boot/bootfs.h>
#include <zircon/errors.h>

#include <cstdint>
#include <iterator>
#include <limits>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/string.h>
#include <fbl/string_piece.h>
#include <zxtest/zxtest.h>

namespace {

constexpr uint64_t kVmoSize = 1024 * 1024;

struct BootfsEntry {
  fbl::String name;
  fbl::StringPiece data;
};

// helper for creating a bootfs to use
void CreateBootfs(BootfsEntry* entries, size_t num_entries, zx::vmo* vmo_out) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kVmoSize, 0, &vmo));

  uint32_t offset = static_cast<uint32_t>(sizeof(zbi_bootfs_header_t));
  for (size_t i = 0; i < num_entries; ++i) {
    auto& entry = entries[i];
    // Must be page-aligned
    const uint32_t data_offset = static_cast<uint32_t>(ZBI_BOOTFS_PAGE_SIZE * (i + 1));

    zbi_bootfs_dirent_t dirent = {
        .name_len = static_cast<uint32_t>(entry.name.size() + 1),
        .data_len = static_cast<uint32_t>(entry.data.size()),
        .data_off = data_offset,
    };

    // Write header & name
    ASSERT_OK(vmo.write(&dirent, offset, sizeof(dirent)));
    ASSERT_OK(vmo.write(entry.name.c_str(), offset + sizeof(dirent), dirent.name_len));
    // Entries must be 32-bit aligned
    offset += ZBI_BOOTFS_DIRENT_SIZE(dirent.name_len);

    // Write data
    ASSERT_OK(vmo.write(entry.data.data(), data_offset, entry.data.size()));
  }

  zbi_bootfs_header_t header = {};
  header.magic = ZBI_BOOTFS_MAGIC;
  header.dirsize = static_cast<uint32_t>(offset - sizeof(header));

  ASSERT_OK(vmo.write(&header, 0, sizeof(header)));
  *vmo_out = std::move(vmo);
}

TEST(ParserTestCase, ParseWithoutInit) {
  bootfs::Parser parser;

  ASSERT_EQ(parser.Parse([](const zbi_bootfs_dirent_t* entry) { return ZX_OK; }), ZX_ERR_BAD_STATE);
}

TEST(ParserTestCase, InitTwice) {
  zx::vmo vmo;
  ASSERT_NO_FAILURES(CreateBootfs(nullptr, 0, &vmo));

  bootfs::Parser parser;
  ASSERT_OK(parser.Init(zx::unowned_vmo(vmo)));
  ASSERT_EQ(parser.Init(zx::unowned_vmo(vmo)), ZX_ERR_BAD_STATE);
}

TEST(ParserTestCase, InitBadMagic) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kVmoSize, 0, &vmo));

  zbi_bootfs_header_t header = {};
  header.magic = ZBI_BOOTFS_MAGIC ^ 1;
  header.dirsize = 0;

  ASSERT_OK(vmo.write(&header, 0, sizeof(header)));

  bootfs::Parser parser;
  ASSERT_EQ(parser.Init(zx::unowned_vmo(vmo)), ZX_ERR_IO);
}

TEST(ParserTestCase, InitShortHeader) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(0, 0, &vmo));

  bootfs::Parser parser;
  ASSERT_EQ(parser.Init(zx::unowned_vmo(vmo)), ZX_ERR_OUT_OF_RANGE);
}

TEST(ParserTestCase, InitCantMap) {
  zx::vmo vmo;
  ASSERT_NO_FAILURES(CreateBootfs(nullptr, 0, &vmo));
  ASSERT_OK(vmo.replace(ZX_RIGHT_READ, &vmo));

  bootfs::Parser parser;
  ASSERT_EQ(parser.Init(zx::unowned_vmo(vmo)), ZX_ERR_ACCESS_DENIED);
}

TEST(ParserTestCase, ExtraHeaderData) {
  // This must be built manually rather than with CreateBootfs because it has an invalid format.
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(2 * ZBI_BOOTFS_PAGE_SIZE, 0, &vmo));

  // Set dirsize such that there's data remaining after parsing all full dirents
  zbi_bootfs_header_t header = {.magic = ZBI_BOOTFS_MAGIC, .dirsize = 1};
  ASSERT_OK(vmo.write(&header, 0, sizeof(header)));

  bootfs::Parser parser;
  ASSERT_OK(parser.Init(zx::unowned_vmo(vmo)));
  ASSERT_EQ(parser.Parse([](const zbi_bootfs_dirent_t* entry) { return ZX_OK; }), ZX_ERR_IO);
}

TEST(ParserTestCase, DirentNameLengthOutOfBounds) {
  // This must be built manually rather than with CreateBootfs because it has an invalid format.
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(2 * ZBI_BOOTFS_PAGE_SIZE, 0, &vmo));

  // Create a dirent header with a name_len that is out of the dirsize bounds, by not including the
  // right name_len in dirsize
  zbi_bootfs_header_t header = {.magic = ZBI_BOOTFS_MAGIC,
                                .dirsize = sizeof(zbi_bootfs_dirent_t) + 1};
  zbi_bootfs_dirent_t dirent = {.name_len = 2, .data_len = 10, .data_off = ZBI_BOOTFS_PAGE_SIZE};

  ASSERT_OK(vmo.write(&header, 0, sizeof(header)));
  ASSERT_OK(vmo.write(&dirent, sizeof(header), sizeof(dirent)));

  bootfs::Parser parser;
  ASSERT_OK(parser.Init(zx::unowned_vmo(vmo)));
  ASSERT_EQ(parser.Parse([](const zbi_bootfs_dirent_t* entry) { return ZX_OK; }), ZX_ERR_IO);
}

TEST(ParserTestCase, DirentDataNotPageAligned) {
  // This must be built manually rather than with CreateBootfs because it has an invalid format.
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(2 * ZBI_BOOTFS_PAGE_SIZE, 0, &vmo));

  // Create a dirent header that tries to put its data right after the header.
  const char* dirent_name = "foo";
  uint32_t name_len = static_cast<uint32_t>(strlen(dirent_name)) + 1;
  uint32_t dirsize = ZBI_BOOTFS_DIRENT_SIZE(name_len);
  zbi_bootfs_header_t header = {.magic = ZBI_BOOTFS_MAGIC, .dirsize = dirsize};
  // Note .data_off is not page aligned here
  zbi_bootfs_dirent_t dirent = {.name_len = name_len, .data_len = 10, .data_off = dirsize};

  ASSERT_OK(vmo.write(&header, 0, sizeof(header)));
  ASSERT_OK(vmo.write(&dirent, sizeof(header), sizeof(dirent)));
  ASSERT_OK(vmo.write(dirent_name, sizeof(header) + sizeof(dirent), name_len));

  bootfs::Parser parser;
  ASSERT_OK(parser.Init(zx::unowned_vmo(vmo)));
  ASSERT_EQ(parser.Parse([](const zbi_bootfs_dirent_t* entry) { return ZX_OK; }), ZX_ERR_IO);
}

TEST(ParserTestCase, DirentDataOutOfBounds) {
  // This must be built manually rather than with CreateBootfs because it has an invalid format.
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(2 * ZBI_BOOTFS_PAGE_SIZE, 0, &vmo));

  // Create a dirent header whose data length extends beyond the end of the VMO
  const char* dirent_name = "foo";
  uint32_t name_len = static_cast<uint32_t>(strlen(dirent_name)) + 1;
  uint32_t dirsize = ZBI_BOOTFS_DIRENT_SIZE(name_len);
  zbi_bootfs_header_t header = {.magic = ZBI_BOOTFS_MAGIC, .dirsize = dirsize};
  // Note .data_len extends past the VMO size of 2*page_size
  zbi_bootfs_dirent_t dirent = {
      .name_len = name_len, .data_len = ZBI_BOOTFS_PAGE_SIZE + 1, .data_off = ZBI_BOOTFS_PAGE_SIZE};

  ASSERT_OK(vmo.write(&header, 0, sizeof(header)));
  ASSERT_OK(vmo.write(&dirent, sizeof(header), sizeof(dirent)));
  ASSERT_OK(vmo.write(dirent_name, sizeof(header) + sizeof(dirent), name_len));

  bootfs::Parser parser;
  ASSERT_OK(parser.Init(zx::unowned_vmo(vmo)));
  ASSERT_EQ(parser.Parse([](const zbi_bootfs_dirent_t* entry) { return ZX_OK; }), ZX_ERR_IO);
}

TEST(ParserTestCase, DirentDataFieldsOverflowProtected) {
  // This must be built manually rather than with CreateBootfs because it has an invalid format.
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(2 * ZBI_BOOTFS_PAGE_SIZE, 0, &vmo));

  // Create a dirent header whose data length extends beyond the end of the VMO
  const char* dirent_name = "foo";
  uint32_t name_len = static_cast<uint32_t>(strlen(dirent_name)) + 1;
  uint32_t dirsize = ZBI_BOOTFS_DIRENT_SIZE(name_len);
  zbi_bootfs_header_t header = {.magic = ZBI_BOOTFS_MAGIC, .dirsize = dirsize};
  // Note .data_off + .data_len overflows uint32_t
  zbi_bootfs_dirent_t dirent = {.name_len = name_len,
                                .data_len = std::numeric_limits<uint32_t>::max(),
                                .data_off = ZBI_BOOTFS_PAGE_SIZE};

  ASSERT_OK(vmo.write(&header, 0, sizeof(header)));
  ASSERT_OK(vmo.write(&dirent, sizeof(header), sizeof(dirent)));
  ASSERT_OK(vmo.write(dirent_name, sizeof(header) + sizeof(dirent), name_len));

  bootfs::Parser parser;
  ASSERT_OK(parser.Init(zx::unowned_vmo(vmo)));
  ASSERT_EQ(parser.Parse([](const zbi_bootfs_dirent_t* entry) { return ZX_OK; }), ZX_ERR_IO);
}

TEST(ParserTestCase, DirentNameNotNulTerminated) {
  // This must be built manually rather than with CreateBootfs because it has an invalid format.
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(2 * ZBI_BOOTFS_PAGE_SIZE, 0, &vmo));

  // Create a dirent header with a non-nul terminated name.
  const char* dirent_name = "foo";
  // Note name_len does not include nul terminator.
  uint32_t name_len = static_cast<uint32_t>(strlen(dirent_name));
  uint32_t dirsize = ZBI_BOOTFS_DIRENT_SIZE(name_len);
  zbi_bootfs_header_t header = {.magic = ZBI_BOOTFS_MAGIC, .dirsize = dirsize};
  zbi_bootfs_dirent_t dirent = {
      .name_len = name_len, .data_len = 10, .data_off = ZBI_BOOTFS_PAGE_SIZE};

  ASSERT_OK(vmo.write(&header, 0, sizeof(header)));
  ASSERT_OK(vmo.write(&dirent, sizeof(header), sizeof(dirent)));
  ASSERT_OK(vmo.write(dirent_name, sizeof(header) + sizeof(dirent), name_len));

  bootfs::Parser parser;
  ASSERT_OK(parser.Init(zx::unowned_vmo(vmo)));
  ASSERT_EQ(parser.Parse([](const zbi_bootfs_dirent_t* entry) { return ZX_OK; }),
            ZX_ERR_INVALID_ARGS);
}

TEST(ParserTestCase, PathSeparatorAtStartOfDirentName) {
  BootfsEntry entries[] = {
      {
          .name = "/foo",
          .data = "lorem ipsum",
      },
  };
  zx::vmo vmo;
  ASSERT_NO_FAILURES(CreateBootfs(entries, std::size(entries), &vmo));

  bootfs::Parser parser;
  ASSERT_OK(parser.Init(zx::unowned_vmo(vmo)));
  ASSERT_EQ(parser.Parse([](const zbi_bootfs_dirent_t* entry) { return ZX_OK; }),
            ZX_ERR_INVALID_ARGS);
}

TEST(ParserTestCase, ParseSuccess) {
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
  ASSERT_NO_FAILURES(CreateBootfs(entries, std::size(entries), &vmo));

  bootfs::Parser parser;
  ASSERT_OK(parser.Init(zx::unowned_vmo(vmo)));

  const zbi_bootfs_dirent_t* parsed_entries[3];
  size_t seen = 0;
  EXPECT_OK(parser.Parse([&entries, &parsed_entries, &seen](const zbi_bootfs_dirent_t* entry) {
    if (seen >= std::size(entries)) {
      return ZX_ERR_BAD_STATE;
    }
    parsed_entries[seen] = entry;
    ++seen;
    return ZX_OK;
  }));
  ASSERT_EQ(seen, std::size(entries));

  for (size_t i = 0; i < seen; ++i) {
    const auto& real_entry = entries[i];
    const auto& parsed_entry = parsed_entries[i];
    ASSERT_EQ(parsed_entry->name_len, real_entry.name.size() + 1);
    ASSERT_EQ(parsed_entry->data_len, real_entry.data.size());
    ASSERT_BYTES_EQ(reinterpret_cast<const uint8_t*>(parsed_entry->name),
                    reinterpret_cast<const uint8_t*>(real_entry.name.c_str()),
                    parsed_entry->name_len);

    uint8_t buffer[parsed_entry->data_len];
    ASSERT_OK(vmo.read(buffer, parsed_entry->data_off, sizeof(buffer)));
    ASSERT_BYTES_EQ(static_cast<const uint8_t*>(buffer),
                    reinterpret_cast<const uint8_t*>(real_entry.data.data()), sizeof(buffer));
  }
}

}  // namespace
