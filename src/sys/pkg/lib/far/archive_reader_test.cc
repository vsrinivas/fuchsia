// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/pkg/lib/far/archive_reader.h"

#include <array>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/lib/files/file_descriptor.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace archive {

namespace {

constexpr size_t kChunkSize = 4096;

// exampleArchive is a valid FAR that is compliant to the FAR spec.
// Note that in order to fill in the content chunks with non-zero bytes,
// prepareExampleArchive() can be called.
constexpr const std::array<uint8_t, 4 * kChunkSize> kExampleArchive = []() {
  std::array<uint8_t, 4 * kChunkSize> buffer = {
      // The magic header.
      0xc8, 0xbf, 0x0b, 0x48, 0xad, 0xab, 0xc5, 0x11,
      // The length of the index entries.
      0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      // The chunk type.
      0x44, 0x49, 0x52, 0x2d, 0x2d, 0x2d, 0x2d, 0x2d,
      // The offset to the chunk.
      0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      // The length of the chunk.
      0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      // The chunk type.
      0x44, 0x49, 0x52, 0x4e, 0x41, 0x4d, 0x45, 0x53,
      // The offset to the chunk.
      0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      // The length of the chunk.
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      // A directory chunk.
      // The directory table entry for path "a".
      0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00,
      // The directory table entry for path "b".
      0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00,
      // The directory table entry for path "c".
      0x02, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00,
      // The directory names chunk with one byte of padding:
      // 'a','b',  'd',  'i',  'r',  '/',  'c', 0x00
      0x61, 0x62, 0x64, 0x69, 0x72, 0x2f, 0x63, 0x00};
  buffer[kChunkSize] = 'a';
  buffer[kChunkSize + 1] = '\n';
  buffer[kChunkSize * 2] = 'b';
  buffer[kChunkSize * 2 + 1] = '\n';
  buffer[kChunkSize * 3] = 'd';
  buffer[kChunkSize * 3 + 1] = 'i';
  buffer[kChunkSize * 3 + 2] = 'r';
  buffer[kChunkSize * 3 + 3] = '/';
  buffer[kChunkSize * 3 + 4] = 'c';
  buffer[kChunkSize * 3 + 5] = '\n';
  return buffer;
}();

// kEmptyArchive is a minimal FAR, but has no directory chunk, hence not compliant to the
// FAR spec.
constexpr uint8_t kEmptyArchive[]{0xc8, 0xbf, 0xb, 0x48, 0xad, 0xab, 0xc5, 0x11,
                                  0x0,  0x0,  0x0, 0x0,  0x0,  0x0,  0x0,  0x0};

bool TestReadArchive(const uint8_t* data, ssize_t len) {
  files::ScopedTempDir dir;
  std::string path;
  EXPECT_TRUE(dir.NewTempFile(&path));

  fbl::unique_fd fd(open(path.c_str(), O_RDWR));
  EXPECT_TRUE(fd);

  EXPECT_TRUE(fxl::WriteFileDescriptor(fd.get(), reinterpret_cast<const char*>(data), len));

  lseek(fd.get(), 0, SEEK_SET);
  return ArchiveReader(std::move(fd)).Read();
}

TEST(ValidateArchive, EmptyArchiveIsInvalid) {
  // The empty archive is invalid according to spec.
  // It does not contain the mandatory directory chunk.
  ASSERT_FALSE(TestReadArchive(kEmptyArchive, sizeof(kEmptyArchive)));
}

TEST(ValidateArchive, ValidExampleArchive) {
  // Valid example archive according to spec.
  ASSERT_TRUE(TestReadArchive(kExampleArchive.data(), kExampleArchive.size()));
}

}  // namespace

}  // namespace archive
