// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/utils/fd_writer.h"

#include <fcntl.h>
#include <lib/stdcompat/span.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>

#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/storage/volume_image/utils/fd_test_helper.h"

namespace storage::volume_image {
namespace {

TEST(FdWriterTest, CreateFromEmptyPathIsError) { ASSERT_TRUE(FdWriter::Create("").is_error()); }

TEST(FdWriterTest, CreateFromPathToInexistentFileIsError) {
  ASSERT_TRUE(
      FdWriter::Create("myverylongpaththatdoesnotexistbecauseitsimplydoesnot.nonexistingextension")
          .is_error());
}

TEST(FdWriterTest, CreateFromExistingFileIsOk) {
  auto temp_file_result = TempFile::Create();
  ASSERT_TRUE(temp_file_result.is_ok()) << temp_file_result.error();
  TempFile file = temp_file_result.take_value();

  auto fd_reader_result = FdWriter::Create(file.path());
  ASSERT_TRUE(fd_reader_result.is_ok());

  auto fd_reader = fd_reader_result.take_value();
  EXPECT_EQ(fd_reader.name(), file.path());
}

// Wrapper on top of posix, to guarantee to write all contents to the file or fail.
void Read(int fd, cpp20::span<char> buffer) {
  uint64_t read_bytes = 0;
  while (read_bytes < buffer.size()) {
    auto return_code = read(fd, buffer.data() + read_bytes, buffer.size() - read_bytes);
    ASSERT_GT(return_code, 0);
    read_bytes += return_code;
  }
}

// Random contents for a file.
constexpr std::string_view kFileContents = "12345678901234567890abcedf12345";

TEST(FdWriterTest, WriteUpdateContentsReturnsNoError) {
  auto temp_file_result = TempFile::Create();
  ASSERT_TRUE(temp_file_result.is_ok()) << temp_file_result.error();
  TempFile file = temp_file_result.take_value();

  auto fd_writer_or_error = FdWriter::Create(file.path());
  ASSERT_TRUE(fd_writer_or_error.is_ok()) << fd_writer_or_error.error();
  auto writer = fd_writer_or_error.take_value();
  auto write_result = writer.Write(
      0, cpp20::span(reinterpret_cast<const uint8_t*>(kFileContents.data()), kFileContents.size()));
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();

  std::vector<char> buffer(kFileContents.size(), 0);
  fbl::unique_fd target_fd(open(file.path().data(), O_RDONLY));
  ASSERT_TRUE(target_fd.is_valid());
  ASSERT_NO_FATAL_FAILURE(Read(target_fd.get(), buffer));

  EXPECT_TRUE(memcmp(kFileContents.data(), buffer.data(), kFileContents.size()) == 0);
}

TEST(FdWriterTest, WriteReturnsCorrectContentsAtOffset) {
  constexpr uint64_t kOffset = 10;
  auto temp_file_result = TempFile::Create();
  ASSERT_TRUE(temp_file_result.is_ok()) << temp_file_result.error();
  TempFile file = temp_file_result.take_value();

  auto fd_writer_or_error = FdWriter::Create(file.path());
  ASSERT_TRUE(fd_writer_or_error.is_ok()) << fd_writer_or_error.error();
  auto writer = fd_writer_or_error.take_value();
  auto write_result = writer.Write(
      kOffset,
      cpp20::span(reinterpret_cast<const uint8_t*>(kFileContents.data()), kFileContents.size())
          .subspan(kOffset));
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();

  std::vector<char> buffer(kFileContents.size(), 0);
  fbl::unique_fd target_fd(open(file.path().data(), O_RDONLY));
  ASSERT_TRUE(target_fd.is_valid());
  ASSERT_NO_FATAL_FAILURE(Read(target_fd.get(), buffer));

  EXPECT_TRUE(memcmp(kFileContents.data() + kOffset, buffer.data() + kOffset,
                     kFileContents.size() - kOffset) == 0);
}

TEST(FdWriterTest, WritesAreIdempotent) {
  auto temp_file_result = TempFile::Create();
  ASSERT_TRUE(temp_file_result.is_ok()) << temp_file_result.error();
  TempFile file = temp_file_result.take_value();

  auto fd_writer_or_error = FdWriter::Create(file.path());
  ASSERT_TRUE(fd_writer_or_error.is_ok()) << fd_writer_or_error.error();
  auto writer = fd_writer_or_error.take_value();

  // If writes are idempotent, we should see the same written value as if we written once.
  for (unsigned int i = 0; i < kFileContents.size() - 1; i++) {
    auto write_result = writer.Write(
        i, cpp20::span(reinterpret_cast<const uint8_t*>(kFileContents.data()), kFileContents.size())
               .subspan(i));
    ASSERT_TRUE(write_result.is_ok()) << write_result.error();
  }

  std::vector<char> buffer(kFileContents.size(), 0);
  fbl::unique_fd target_fd(open(file.path().data(), O_RDONLY));
  ASSERT_TRUE(target_fd.is_valid());
  ASSERT_NO_FATAL_FAILURE(Read(target_fd.get(), buffer));

  EXPECT_TRUE(memcmp(kFileContents.data(), buffer.data(), kFileContents.size()) == 0);
}

TEST(FdWriterTest, WritingPastEndOfFileIsOk) {
  auto temp_file_result = TempFile::Create();
  ASSERT_TRUE(temp_file_result.is_ok()) << temp_file_result.error();
  TempFile file = temp_file_result.take_value();

  fbl::unique_fd target_fd(open(file.path().data(), O_RDONLY));
  ASSERT_TRUE(target_fd.is_valid());

  auto fd_writer_or_error = FdWriter::Create(file.path());
  ASSERT_TRUE(fd_writer_or_error.is_ok()) << fd_writer_or_error.error();
  auto writer = fd_writer_or_error.take_value();

  // Try to write past the end.
  EXPECT_TRUE(writer
                  .Write(0, cpp20::span(reinterpret_cast<const uint8_t*>(kFileContents.data()),
                                        kFileContents.size()))
                  .is_ok());

  // Try to start writing at the end.
  EXPECT_TRUE(writer
                  .Write(kFileContents.size(),
                         cpp20::span(reinterpret_cast<const uint8_t*>(kFileContents.data()),
                                     kFileContents.size()))
                  .is_ok());

  // Try to start writing at random offset
  EXPECT_TRUE(writer
                  .Write(4 * kFileContents.size(),
                         cpp20::span(reinterpret_cast<const uint8_t*>(kFileContents.data()),
                                     kFileContents.size()))
                  .is_ok());

  std::vector<char> buffer(kFileContents.size() * 5, 0);
  ASSERT_NO_FATAL_FAILURE(Read(target_fd.get(), buffer));

  // First write is ok.
  EXPECT_TRUE(memcmp(kFileContents.data(), buffer.data(), kFileContents.size()) == 0);

  // Second write is ok.
  EXPECT_TRUE(memcmp(kFileContents.data(), buffer.data() + kFileContents.size(),
                     kFileContents.size()) == 0);

  // Third write is ok.
  EXPECT_TRUE(memcmp(kFileContents.data(), buffer.data() + kFileContents.size() * 4,
                     kFileContents.size()) == 0);
}

}  // namespace
}  // namespace storage::volume_image
