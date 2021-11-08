// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/pkg/lib/far/cpp/archive_reader.h"

#include <fcntl.h>

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

bool ReadArchive(const std::string& path) {
  fbl::unique_fd fd(open(path.c_str(), O_RDONLY));
  EXPECT_TRUE(fd);

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

TEST(ValidateArchive, GeneratedArchiveIsInvalid) {
  // Generated invalid archives from the "//src/sys/pkg/testing/invalid-fars:resource"
  // target to test various constraints mandated by the spec.
  const std::array<std::string, 33> test_files = {
      "invalid-magic-bytes.far",
      "index-entries-length-not-a-multiple-of-24-bytes.far",
      "directory-names-index-entry-before-directory-index-entry.far",
      "two-directory-index-entries.far",
      "two-directory-names-index-entries.far",
      "no-directory-index-entry.far",
      "no-directory-names-index-entry.far",
      "directory-chunk-length-not-a-multiple-of-32-bytes.far",
      "directory-chunk-not-tightly-packed.far",
      "duplicate-index-entries-of-unknown-type.far",
      "path-data-offset-too-large.far",
      "path-data-length-too-large.far",
      "directory-entries-not-sorted.far",
      "directory-entries-with-same-name.far",
      "directory-names-chunk-length-not-a-multiple-of-8-bytes.far",
      "directory-names-chunk-not-tightly-packed.far",
      "directory-names-chunk-before-directory-chunk.far",
      "directory-names-chunk-overlaps-directory-chunk.far",
      "zero-length-name.far",
      "name-with-null-character.far",
      "name-with-leading-slash.far",
      "name-with-trailing-slash.far",
      "name-with-empty-segment.far",
      "name-with-dot-segment.far",
      "name-with-dot-dot-segment.far",
      "content-chunk-starts-early.far",
      "content-chunk-starts-late.far",
      "second-content-chunk-starts-early.far",
      "second-content-chunk-starts-late.far",
      "content-chunk-not-zero-padded.far",
      "content-chunk-overlap.far",
      "content-chunk-not-tightly-packed.far",
      "content-chunk-offset-past-end-of-file.far"};
  for (const auto& file : test_files) {
    auto path = "/pkg/data/invalid-fars/" + file;
    ASSERT_FALSE(ReadArchive(path)) << "Invalid archive passed validation: " << path;
  }
}

}  // namespace

// Tests for DirNameOK must be placed in the archive namespace, see
// https://github.com/google/googletest/blob/3306848f697568aacf4bcca330f6bdd5ce671899/googletest/include/gtest/gtest_prod.h#L55

TEST(ValidateDirName, NameIsValid) {
  const auto test_names = {"a",   "a/a",  "a/a/a", ".a",   "a.",   "..a",
                           "a..", "a./a", "a../a", "a/.a", "a/..a"};

  // The content of the /tmp dir does not matter, since we are not
  // going to Read() from it anyway. We just need a valid ArchiveReader
  // object to run the FRIEND_TEST on ArchiveReader::DirNameOK().
  fbl::unique_fd fd(open("/tmp", O_RDONLY));
  ArchiveReader r(std::move(fd));
  for (const auto& name : test_names) {
    ASSERT_TRUE(r.DirNameOK(name)) << "Valid dir name was not accepted: " << name;
  }
}

TEST(ValidateDirName, NameIsInvalid) {
  using namespace std::string_literals;
  const auto test_names = {"/"s,     "/a"s,   "a/"s,   "aa/"s,    "\0"s,    "a\0"s, "\0a"s,
                           "a/\0"s,  "\0/a"s, "a//a"s, "a/a//a"s, "."s,     "./a"s, "a/."s,
                           "a/./a"s, ".."s,   "../a"s, "a/.."s,   "a/../a"s};

  // The content of the /tmp dir does not matter, since we are not
  // going to Read() from it anyway. We just need a valid ArchiveReader
  // object to run the FRIEND_TEST on ArchiveReader::DirNameOK().
  fbl::unique_fd fd(open("/tmp", O_RDONLY));
  ArchiveReader reader(std::move(fd));
  for (const auto& name : test_names) {
    ASSERT_FALSE(reader.DirNameOK(name)) << "Invalid dir name was accepted: " << name;
  }
}

}  // namespace archive
