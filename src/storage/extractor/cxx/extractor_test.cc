// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/extractor/c/extractor.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zircon/errors.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <tuple>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/storage/extractor/cxx/extractor.h"

namespace extractor {
namespace {

std::tuple<fbl::unique_fd, fbl::unique_fd> setup_fds() {
  char kInFile[20] = "/tmp/payload.XXXXXX";
  char kOutFile[20] = "/tmp/payload.XXXXXX";

  fbl::unique_fd out_file(mkstemp(kOutFile));
  fbl::unique_fd in_file(mkstemp(kInFile));
  EXPECT_TRUE(out_file);
  EXPECT_TRUE(in_file);
  const uint16_t kBlockSize = 8192;
  const uint16_t kBlockCount = 10;
  char buf[kBlockSize];
  for (int i = 0; i < kBlockCount; i++) {
    memset(buf, i, sizeof(buf));
    EXPECT_EQ(write(in_file.get(), buf, sizeof(buf)), static_cast<ssize_t>(sizeof(buf)));
  }
  return std::make_tuple(std::move(in_file), std::move(out_file));
}

TEST(Extractor, CreateInvalidAlignment) {
  ExtractorOptions options;
  options.add_checksum = false;
  options.force_dump_pii = false;
  options.alignment = 0;
  fbl::unique_fd in;
  fbl::unique_fd out;
  auto extractor = Extractor::Create(std::move(in), options, std::move(out));
  ASSERT_TRUE(extractor.is_error());
  ASSERT_EQ(extractor.error_value(), ZX_ERR_INVALID_ARGS);
}

TEST(Extractor, CreateFailureWithInvalidFd) {
  ExtractorOptions options;
  options.add_checksum = false;
  options.force_dump_pii = false;
  options.alignment = 1;
  fbl::unique_fd in;
  fbl::unique_fd out;
  auto extractor = Extractor::Create(std::move(in), options, std::move(out));
  ASSERT_TRUE(extractor.is_error());
  ASSERT_EQ(extractor.error_value(), ZX_ERR_IO);
}

TEST(Extractor, Create) {
  ExtractorOptions options;
  options.add_checksum = false;
  options.force_dump_pii = false;
  options.alignment = 1;
  fbl::unique_fd in;
  fbl::unique_fd out;
  std::tie(in, out) = setup_fds();
  auto extractor_or = Extractor::Create(std::move(in), options, out.duplicate());
  ASSERT_TRUE(extractor_or.is_ok());

  // If we don't issue a write, then file size should be 0.
  struct stat stats;
  ASSERT_EQ(fstat(out.get(), &stats), 0);
  ASSERT_EQ(stats.st_size, 0);
}

TEST(Extractor, AddInvalidExtent) {
  ExtractorOptions options;
  options.add_checksum = false;
  options.force_dump_pii = false;
  options.alignment = 1;
  fbl::unique_fd in;
  fbl::unique_fd out;
  std::tie(in, out) = setup_fds();
  auto extractor_or = Extractor::Create(std::move(in), options, std::move(out));
  ASSERT_TRUE(extractor_or.is_ok());
  auto extractor = std::move(extractor_or.value());
  ExtentProperties properties = {.extent_kind = ExtentKind::Data,
                                 .data_kind = DataKind::Unmodified};
  ASSERT_EQ(extractor->Add(10, 0, properties).error_value(), ZX_ERR_OUT_OF_RANGE);
}

void verify_block(int fd, size_t size, off_t offset, uint8_t content) {
  uint8_t buffer[size];
  ASSERT_EQ(pread(fd, buffer, size, offset), static_cast<ssize_t>(size));
  for (size_t i = 0; i < size; i++) {
    ASSERT_EQ(buffer[i], content);
  }
}

TEST(Extractor, AddUnaligned) {
  ExtractorOptions options;
  options.add_checksum = false;
  options.force_dump_pii = false;
  options.alignment = 1024;

  fbl::unique_fd in;
  fbl::unique_fd out;
  std::tie(in, out) = setup_fds();
  auto extractor_or = Extractor::Create(std::move(in), options, std::move(out));
  ASSERT_TRUE(extractor_or.is_ok());
  auto extractor = std::move(extractor_or.value());
  ExtentProperties properties = {.extent_kind = ExtentKind::Data,
                                 .data_kind = DataKind::Unmodified};
  // Alignment is 1024 bytes and test is adding extent at offset 10.
  ASSERT_EQ(extractor->Add(10, 1, properties).error_value(), ZX_ERR_INVALID_ARGS);
}

TEST(Extractor, Write) {
  constexpr uint16_t kBlockSize = 8192;
  ExtractorOptions options;
  options.add_checksum = false;
  options.force_dump_pii = false;
  options.alignment = kBlockSize;

  fbl::unique_fd in;
  fbl::unique_fd out;
  std::tie(in, out) = setup_fds();
  auto extractor_or = Extractor::Create(in.duplicate(), options, out.duplicate());
  ASSERT_TRUE(extractor_or.is_ok());
  auto extractor = std::move(extractor_or.value());
  ExtentProperties properties = {.extent_kind = ExtentKind::Data,
                                 .data_kind = DataKind::Unmodified};
  // Add a block starting at offset 5*8192
  ASSERT_TRUE(extractor->Add(5 * kBlockSize, kBlockSize, properties).is_ok());
  // Add two blocks starting at offset 8192
  ASSERT_TRUE(extractor->Add(1 * kBlockSize, 2 * kBlockSize, properties).is_ok());

  ASSERT_TRUE(extractor->Write().is_ok());

  EXPECT_EQ(lseek(out.get(), SEEK_SET, 0), 0);
  struct stat stats;
  EXPECT_EQ(fstat(out.get(), &stats), 0);

  // We should have 5 block in image file. Header, extent cluster, and 3 data blocks.
  ASSERT_EQ(stats.st_size, 5 * kBlockSize);

  // We know the content of data blocks written. Verify that those blocks are
  // in the image file.
  // input file's block 'n' contens all 'n'.
  // Skip first two block of the image file as they contain image header. Block
  // 2, 3 4 should have out data.
  verify_block(out.get(), kBlockSize, 2 * kBlockSize, 1);
  verify_block(out.get(), kBlockSize, 3 * kBlockSize, 2);
  verify_block(out.get(), kBlockSize, 4 * kBlockSize, 5);
}

}  // namespace

}  // namespace extractor
