// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <getopt.h>
#include <lib/zx/vmo.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/boot/image.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

#include <fbl/unique_fd.h>
#include <zbi-bootfs/zbi-bootfs.h>
#include <zxtest/zxtest.h>

namespace {
constexpr char kZstdZbi[] = "generated-zstd.zbi";
constexpr char kZstdZbiFilename[] = "payload_1";

constexpr char kLz4fZbi[] = "generated-lz4f.zbi";
constexpr char kLz4fZbiFilename[] = "payload_2";

static std::string MakeZbiPath(const std::string& filename) {
  const char* root_dir = getenv("TEST_ROOT_DIR");
  if (root_dir == nullptr) {
    root_dir = "";
  }
  return std::string(root_dir) + "/testdata/zbi-bootfs/" + filename;
}

static void AssertHasContents(const zbi_bootfs::Entry& entry, const char* contents) {
  auto buffer = std::make_unique<std::byte[]>(entry.size);
  zx_status_t status = entry.vmo.read(buffer.get(), 0, entry.size);
  ASSERT_EQ(ZX_OK, status);
  ASSERT_BYTES_EQ(contents, buffer.get(), strlen(contents));
}

void WriteArbitraryZbi(const std::string& filename, const uint8_t* buffer, size_t buffer_len) {
  fbl::unique_fd fd = fbl::unique_fd(open(filename.c_str(), O_RDWR));
  ASSERT_TRUE(fd);

  write(fd.get(), buffer, buffer_len);
}

TEST(ZbiBootfsTestCase, InitSuccess) {
  zbi_bootfs::ZbiBootfsParser parser;
  const std::string input = MakeZbiPath(kZstdZbi);

  // Check good input
  zx::vmo vmo_out;
  ASSERT_EQ(ZX_OK, parser.Init(input.c_str()));
}

TEST(ZbiBootfsTestCase, InitBadInput) {
  zbi_bootfs::ZbiBootfsParser parser;

  // Check bad input
  const char* input = nullptr;
  ASSERT_EQ(ZX_ERR_IO, parser.Init(input));
}

TEST(ZbiBootfsTestCase, InitNotCalled) {
  zbi_bootfs::ZbiBootfsParser parser;
  const std::string input = MakeZbiPath(kZstdZbi);
  const char* filename = kZstdZbiFilename;

  zbi_bootfs::Entry entry;

  // Unable to process without Init call. Assert bad state.
  ASSERT_EQ(ZX_ERR_BAD_STATE, parser.ProcessZbi(filename, &entry));
}

TEST(ZbiBootfsTestCase, ProcessZstdZbi) {
  zbi_bootfs::ZbiBootfsParser parser;
  const std::string input = MakeZbiPath(kZstdZbi);
  const char* filename = kZstdZbiFilename;

  zbi_bootfs::Entry entry;
  ASSERT_EQ(ZX_OK, parser.Init(input.c_str()));

  // Check bootfs filename
  // This will return a list of Bootfs entires, plus details of "filename" entry
  ASSERT_EQ(ZX_OK, parser.ProcessZbi(filename, &entry));

  AssertHasContents(entry, "test 1");
}

TEST(ZbiBootfsTestCase, ProcessLz4fZbi) {
  zbi_bootfs::ZbiBootfsParser parser;
  const std::string input = MakeZbiPath(kLz4fZbi);
  const char* filename = kLz4fZbiFilename;

  zbi_bootfs::Entry entry;

  ASSERT_EQ(ZX_OK, parser.Init(input.c_str()));

  // Check bootfs filename
  // This will return a list of Bootfs entires, plus details of "filename" entry
  ASSERT_EQ(ZX_OK, parser.ProcessZbi(filename, &entry));

  AssertHasContents(entry, "test 2");
}

TEST(ZbiBootfsTestCase, ProcessMissing) {
  zbi_bootfs::ZbiBootfsParser parser;
  const std::string input = MakeZbiPath(kZstdZbi);
  zbi_bootfs::Entry entry;

  ASSERT_EQ(ZX_OK, parser.Init(input.c_str()));
  // Check bad payload filename
  // This will return a list of payload (Bootfs) entires
  const char* filename = "";
  ASSERT_EQ(ZX_ERR_NOT_FOUND, parser.ProcessZbi(filename, &entry));
}

TEST(ZbiBootfsTestCase, InitZbiEmptyFile) {
  zbi_bootfs::ZbiBootfsParser parser;
  zbi_bootfs::Entry entry;
  std::string filename = "/data/zbi";

  FILE* file = fopen(filename.c_str(), "w");
  if (file == nullptr) {
    ASSERT_TRUE(false);
  }
  fclose(file);

  ASSERT_EQ(ZX_ERR_IO, parser.Init(filename.c_str()));
}

TEST(ZbiBootfsTestCase, InitZbiEmptyHeader) {
  zbi_bootfs::ZbiBootfsParser parser;
  zbi_bootfs::Entry entry;
  std::string filename = "/data/zbi";

  zbi_header_t header;
  memset(&header, 0, sizeof(zbi_header_t));

  size_t file_size = header.length + sizeof(zbi_header_t);
  uint8_t buffer[file_size];

  memcpy(buffer, &header, sizeof(zbi_header_t));

  WriteArbitraryZbi(filename, buffer, file_size);

  ASSERT_EQ(ZX_ERR_BUFFER_TOO_SMALL, parser.Init(filename.c_str()));
}

TEST(ZbiBootfsTestCase, ProcessNonContainerZbi) {
  zbi_bootfs::ZbiBootfsParser parser;
  zbi_bootfs::Entry entry;
  std::string filename = "/data/zbi";

  // Missing Container Type
  zbi_header_t header;
  memset(&header, 0, sizeof(zbi_header_t));
  header.type = ZBI_TYPE_STORAGE_BOOTFS;
  header.length = 10;

  size_t file_size = header.length + sizeof(zbi_header_t);
  uint8_t buffer[file_size];

  memset(buffer, 0, file_size);
  memcpy(buffer, &header, sizeof(zbi_header_t));

  WriteArbitraryZbi(filename, buffer, file_size);

  ASSERT_EQ(ZX_OK, parser.Init(filename.c_str()));
  ASSERT_EQ(ZX_ERR_BAD_STATE, parser.ProcessZbi("", &entry));

  // Missing Container magic
  memset(&header, 0, sizeof(zbi_header_t));
  header.type = ZBI_TYPE_CONTAINER;
  header.length = 10;

  memset(buffer, 0, file_size);
  memcpy(buffer, &header, sizeof(zbi_header_t));

  WriteArbitraryZbi(filename, buffer, file_size);

  ASSERT_EQ(ZX_OK, parser.Init(filename.c_str()));
  ASSERT_EQ(ZX_ERR_BAD_STATE, parser.ProcessZbi("", &entry));
}

TEST(ZbiBootfsTestCase, ProcessInvalidNestedZbi) {
  zbi_bootfs::ZbiBootfsParser parser;
  zbi_bootfs::Entry entry;
  std::string filename = "/data/zbi";

  zbi_header_t header;
  memset(&header, 0, sizeof(zbi_header_t));
  header.type = ZBI_TYPE_CONTAINER;
  header.extra = ZBI_CONTAINER_MAGIC;
  header.length = 10;

  size_t file_size = header.length + sizeof(zbi_header_t);
  uint8_t buffer[file_size];

  memset(buffer, 0, file_size);
  memcpy(buffer, &header, sizeof(zbi_header_t));

  WriteArbitraryZbi(filename, buffer, file_size);

  ASSERT_EQ(ZX_OK, parser.Init(filename.c_str()));
  ASSERT_EQ(ZX_ERR_BAD_STATE, parser.ProcessZbi("", &entry));
}

TEST(ZbiBootfsTestCase, ProcessEmptyNestedZbi) {
  zbi_bootfs::ZbiBootfsParser parser;
  zbi_bootfs::Entry entry;
  std::string filename = "/data/zbi";

  zbi_header_t header;
  memset(&header, 0, sizeof(zbi_header_t));
  header.type = ZBI_TYPE_CONTAINER;
  header.extra = ZBI_CONTAINER_MAGIC;
  header.length = 32;

  size_t file_size = header.length + sizeof(zbi_header_t);
  uint8_t buffer[file_size];

  memset(buffer, 0, file_size);
  memcpy(buffer, &header, sizeof(zbi_header_t));

  WriteArbitraryZbi(filename, buffer, file_size);

  ASSERT_EQ(ZX_OK, parser.Init(filename.c_str()));
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, parser.ProcessZbi("", &entry));
}

TEST(ZbiBootfsTestCase, ProcessNestedContainerZbi) {
  zbi_bootfs::ZbiBootfsParser parser;
  zbi_bootfs::Entry entry;
  std::string filename = "/data/zbi";

  zbi_header_t header;
  memset(&header, 0, sizeof(zbi_header_t));
  header.type = ZBI_TYPE_CONTAINER;
  header.extra = ZBI_CONTAINER_MAGIC;
  header.length = sizeof(zbi_header_t);

  zbi_header_t nested;
  memset(&nested, 0, sizeof(zbi_header_t));
  nested.type = ZBI_TYPE_CONTAINER;

  size_t file_size = header.length + sizeof(zbi_header_t);
  uint8_t buffer[file_size];

  memset(buffer, 0, file_size);
  memcpy(buffer, &header, sizeof(zbi_header_t));
  memcpy(buffer + sizeof(zbi_header_t), &nested, sizeof(zbi_header_t));

  WriteArbitraryZbi(filename, buffer, file_size);

  ASSERT_EQ(ZX_OK, parser.Init(filename.c_str()));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, parser.ProcessZbi("", &entry));
}

TEST(ZbiBootfsTestCase, ProcessDecompressedNestedZbi) {
  zbi_bootfs::ZbiBootfsParser parser;
  zbi_bootfs::Entry entry;
  std::string filename = "/data/zbi";

  zbi_header_t header;
  memset(&header, 0, sizeof(zbi_header_t));
  header.type = ZBI_TYPE_CONTAINER;
  header.extra = ZBI_CONTAINER_MAGIC;
  header.length = sizeof(zbi_header_t);

  zbi_header_t nested;
  memset(&nested, 0, sizeof(zbi_header_t));
  nested.type = ZBI_TYPE_STORAGE_BOOTFS;

  size_t file_size = header.length + sizeof(zbi_header_t);
  uint8_t buffer[file_size];

  memset(buffer, 0, file_size);
  memcpy(buffer, &header, sizeof(zbi_header_t));
  memcpy(buffer + sizeof(zbi_header_t), &nested, sizeof(zbi_header_t));

  WriteArbitraryZbi(filename, buffer, file_size);

  ASSERT_EQ(ZX_OK, parser.Init(filename.c_str()));
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, parser.ProcessZbi("", &entry));
}

TEST(ZbiBootfsTestCase, ProcessZbiTooLarge) {
  zbi_bootfs::ZbiBootfsParser parser;
  zbi_bootfs::Entry entry;
  std::string filename = "/data/zbi";

  zbi_header_t header;
  memset(&header, 0, sizeof(zbi_header_t));
  header.type = ZBI_TYPE_CONTAINER;
  header.extra = ZBI_CONTAINER_MAGIC;
  header.length = sizeof(zbi_header_t);

  zbi_header_t nested;
  memset(&nested, 0, sizeof(zbi_header_t));
  nested.type = ZBI_TYPE_STORAGE_BOOTFS;
  nested.flags = ZBI_FLAG_STORAGE_COMPRESSED;
  nested.extra = (1 << 30) + 1;  // 1Gib + 1

  size_t file_size = header.length + sizeof(zbi_header_t);
  uint8_t buffer[file_size];

  memset(buffer, 0, file_size);
  memcpy(buffer, &header, sizeof(zbi_header_t));
  memcpy(buffer + sizeof(zbi_header_t), &nested, sizeof(zbi_header_t));

  WriteArbitraryZbi(filename, buffer, file_size);

  ASSERT_EQ(ZX_OK, parser.Init(filename.c_str()));
  ASSERT_EQ(ZX_ERR_FILE_BIG, parser.ProcessZbi("", &entry));
}

}  // namespace
