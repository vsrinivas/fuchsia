// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/ftl/ftl_io.h"

#include <cstdlib>
#include <memory>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/storage/volume_image/ftl/ftl_test_helper.h"

namespace storage::volume_image {
namespace {

constexpr uint64_t kPageSize = 8192;
constexpr uint32_t kOobBytesSize = 16;
constexpr uint64_t kPagesPerBlock = 32;
constexpr uint64_t kBlockCount = 20;

RawNandOptions GetOptions() {
  RawNandOptions options;
  options.oob_bytes_size = kOobBytesSize;
  options.page_size = kPageSize;
  options.pages_per_block = kPagesPerBlock;
  options.page_count = kPagesPerBlock * kBlockCount;
  return options;
}

void FillRandomRange(cpp20::span<uint8_t> data) {
  // fill in a range with random contents.
  unsigned int seed = ::testing::UnitTest::GetInstance()->random_seed();
  size_t start = rand_r(&seed) % (data.size() - 1);
  size_t end = start + rand_r(&seed) % (data.size() - start);
  for (size_t i = start; i <= end; ++i) {
    data[i] = static_cast<uint8_t>(rand_r(&seed));
  }
}

constexpr uint32_t kPageOffset = 48;
constexpr uint32_t kPageCount = 6;

TEST(FtlHandleReaderTest, ContentsAreReadOnePageAtATimeCorrectly) {
  FtlHandle handle;
  InMemoryRawNand raw_nand;
  raw_nand.options = GetOptions();
  std::unique_ptr<InMemoryNdm> ndm =
      std::make_unique<InMemoryNdm>(&raw_nand, kPageSize, kOobBytesSize);

  auto init_result = handle.Init(std::move(ndm));
  ASSERT_TRUE(init_result.is_ok()) << init_result.error();

  // Format the FTL so we can read and write to it.
  ASSERT_EQ(handle.volume().Format(), ZX_OK);

  std::vector<uint8_t> my_data;
  my_data.resize(handle.instance().page_size() * kPageCount, 0);
  FillRandomRange(my_data);

  ASSERT_EQ(handle.volume().Write(kPageOffset, kPageCount, my_data.data()), ZX_OK);

  // Use a reader to pull back that page.
  auto reader = handle.MakeReader();

  std::vector<uint8_t> actual_data;
  actual_data.resize(handle.instance().page_size(), 0);

  for (size_t i = 0; i < kPageCount; ++i) {
    auto read_result = reader->Read((kPageOffset + i) * handle.instance().page_size(), actual_data);
    ASSERT_TRUE(read_result.is_ok()) << read_result.error();

    EXPECT_TRUE(memcmp(actual_data.data(), my_data.data() + (i * handle.instance().page_size()),
                       handle.instance().page_size()) == 0)
        << i;
  }
}

TEST(FtlHandleReaderTest, ContentsAreReadMultiplePageAtATimeCorrectly) {
  FtlHandle handle;
  InMemoryRawNand raw_nand;
  raw_nand.options = GetOptions();
  std::unique_ptr<InMemoryNdm> ndm =
      std::make_unique<InMemoryNdm>(&raw_nand, kPageSize, kOobBytesSize);

  auto init_result = handle.Init(std::move(ndm));
  ASSERT_TRUE(init_result.is_ok()) << init_result.error();

  // Format the FTL so we can read and write to it.
  ASSERT_EQ(handle.volume().Format(), ZX_OK);

  std::vector<uint8_t> my_data;
  my_data.resize(handle.instance().page_size() * kPageCount, 0);
  FillRandomRange(my_data);

  ASSERT_EQ(handle.volume().Write(kPageOffset, kPageCount, my_data.data()), ZX_OK);

  // Use a reader to pull back that page.
  auto reader = handle.MakeReader();

  std::vector<uint8_t> actual_data;
  actual_data.resize(handle.instance().page_size() * kPageCount, 0);

  auto read_result = reader->Read(kPageOffset * handle.instance().page_size(), actual_data);
  ASSERT_TRUE(read_result.is_ok()) << read_result.error();

  EXPECT_TRUE(memcmp(actual_data.data(), my_data.data(), my_data.size()) == 0);
}

TEST(FtlHandleWriterTest, ContentsAreWrittenSinglePageAtATimeCorrectly) {
  FtlHandle handle;
  InMemoryRawNand raw_nand;
  raw_nand.options = GetOptions();
  std::unique_ptr<InMemoryNdm> ndm =
      std::make_unique<InMemoryNdm>(&raw_nand, kPageSize, kOobBytesSize);

  auto init_result = handle.Init(std::move(ndm));
  ASSERT_TRUE(init_result.is_ok()) << init_result.error();

  // Format the FTL so we can read and write to it.
  ASSERT_EQ(handle.volume().Format(), ZX_OK);

  auto writer = handle.MakeWriter();

  std::vector<uint8_t> my_data;
  my_data.resize(handle.instance().page_size() * kPageCount, 0);
  FillRandomRange(my_data);

  auto write_result = writer->Write(kPageOffset * handle.instance().page_size(), my_data);
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();

  // Now read from the volume.
  std::vector<uint8_t> actual_data;
  actual_data.resize(handle.instance().page_size(), 0);

  for (size_t i = 0; i < kPageCount; ++i) {
    ASSERT_EQ(handle.volume().Read(kPageOffset + i, 1, actual_data.data()), ZX_OK);

    EXPECT_TRUE(memcmp(actual_data.data(), my_data.data() + (i * handle.instance().page_size()),
                       handle.instance().page_size()) == 0)
        << i;
  }
}

TEST(FtlHandleWriterTest, ContentsAreWrittenMultiplePagesAtATimeCorrectly) {
  FtlHandle handle;
  InMemoryRawNand raw_nand;
  raw_nand.options = GetOptions();
  std::unique_ptr<InMemoryNdm> ndm =
      std::make_unique<InMemoryNdm>(&raw_nand, kPageSize, kOobBytesSize);

  auto init_result = handle.Init(std::move(ndm));
  ASSERT_TRUE(init_result.is_ok()) << init_result.error();

  // Format the FTL so we can read and write to it.
  ASSERT_EQ(handle.volume().Format(), ZX_OK);

  auto writer = handle.MakeWriter();

  std::vector<uint8_t> my_data;
  my_data.resize(handle.instance().page_size() * kPageCount, 0);
  FillRandomRange(my_data);

  auto write_result = writer->Write(kPageOffset * handle.instance().page_size(), my_data);
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();

  // Now read from the volume.
  std::vector<uint8_t> actual_data;
  actual_data.resize(handle.instance().page_size() * kPageCount, 0);

  ASSERT_EQ(handle.volume().Read(kPageOffset, kPageCount, actual_data.data()), ZX_OK);

  EXPECT_TRUE(memcmp(actual_data.data(), my_data.data(), handle.instance().page_size()) == 0);
}

}  // namespace
}  // namespace storage::volume_image
