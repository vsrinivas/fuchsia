// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/fvm/fvm_image_extend.h"

#include <lib/fit/function.h>
#include <lib/fpromise/result.h>

#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/storage/fvm/format.h"
#include "src/storage/fvm/metadata.h"
#include "src/storage/fvm/metadata_buffer.h"
#include "src/storage/volume_image/fvm/fvm_descriptor.h"
#include "src/storage/volume_image/fvm/options.h"
#include "src/storage/volume_image/utils/block_utils.h"
#include "src/storage/volume_image/utils/reader.h"
#include "src/storage/volume_image/utils/writer.h"

namespace storage::volume_image {
namespace {

// Conforms to a the MetadataBuffer interface required, and allows to inject and unowned buffer if
// necessary. Why is useful for testing.
class MetadataBufferView final : public fvm::MetadataBuffer {
 public:
  MetadataBufferView() : data_(std::vector<uint8_t>()) {}
  explicit MetadataBufferView(cpp20::span<uint8_t> data) : data_(data) {}

  std::unique_ptr<MetadataBuffer> Create(size_t size) const final {
    auto view = std::make_unique<MetadataBufferView>();
    std::get<std::vector<uint8_t>>(view->data_).resize(size);
    return std::move(view);
  }

  void* data() const final {
    return std::visit([](auto& a) { return static_cast<void*>(a.data()); }, data_);
  }

  size_t size() const final {
    return std::visit([](auto& a) { return a.size(); }, data_);
  }

 private:
  mutable std::variant<cpp20::span<uint8_t>, std::vector<uint8_t>> data_;
};

class DelegateReader final : public Reader {
 public:
  DelegateReader(fit::function<fpromise::result<void, std::string>(uint64_t, cpp20::span<uint8_t>)>
                     delegate_reader,
                 uint64_t length)
      : delegate_(std::move(delegate_reader)), length_(length) {}

  uint64_t length() const final { return length_; }

  fpromise::result<void, std::string> Read(uint64_t offset,
                                           cpp20::span<uint8_t> buffer) const final {
    if (offset + buffer.size() > length_) {
      return fpromise::error("DelegateReader::Read attempting to read out of bounds.");
    }
    return delegate_(offset, buffer);
  }

 private:
  fit::function<fpromise::result<void, std::string>(uint64_t, cpp20::span<uint8_t>)> delegate_;
  uint64_t length_ = 0;
};

class DelegateWriter final : public Writer {
 public:
  DelegateWriter(
      fit::function<fpromise::result<void, std::string>(uint64_t, cpp20::span<const uint8_t>)>
          delegate_reader)
      : delegate_(std::move(delegate_reader)) {}

  fpromise::result<void, std::string> Write(uint64_t offset,
                                            cpp20::span<const uint8_t> buffer) final {
    return delegate_(offset, buffer);
  }

 private:
  fit::function<fpromise::result<void, std::string>(uint64_t, cpp20::span<const uint8_t>)>
      delegate_;
};

FvmOptions MakeOptions() {
  static const FvmOptions kOptions = {
      .target_volume_size = 20u * (1u << 20),
      .slice_size = 32 * (1u << 10),
  };

  return kOptions;
}

using internal::MakeHeader;

constexpr uint64_t kDefaultSliceCount = 200;
constexpr uint64_t kDefaultImageSize = 20u * (1 << 20);

void StreamContents(uint64_t offset, cpp20::span<const uint8_t> contents,
                    cpp20::span<uint8_t> buffer) {
  uint64_t written_bytes = 0;

  while (written_bytes < buffer.size()) {
    uint64_t content_offset = (offset + written_bytes) % contents.size();
    uint64_t chunk_size = std::min(static_cast<uint64_t>(buffer.size() - written_bytes),
                                   static_cast<uint64_t>(contents.size() - content_offset));
    memcpy(buffer.data() + written_bytes, contents.data() + content_offset, chunk_size);
    written_bytes += chunk_size;
  }
}

cpp20::span<const uint8_t> AsSpan(const fvm::Metadata& metadata) {
  return cpp20::span<const uint8_t>(reinterpret_cast<const uint8_t*>(metadata.Get()->data()),
                                    metadata.Get()->size());
}

TEST(FvmImageExtendTest, BadFvmSuperblockIsError) {
  DelegateReader reader(
      [](auto offset, auto buffer) {
        memset(buffer.data(), 0, buffer.size());
        return fpromise::ok();
      },
      20u << 20);
  DelegateWriter writer([](auto offset, auto buffer) { return fpromise::ok(); });

  ASSERT_TRUE(FvmImageExtend(reader, MakeOptions(), writer).is_error());
}

TEST(FvmImageExtendTest, ValidHeaderWithBadMetadataIsError) {
  const auto options = MakeOptions();
  const auto header = MakeHeader(options, kDefaultSliceCount);

  // This reader just copyes over and over a valid header. This means, that both Superblocks are
  // 'valid', but the rest of the metadata is wrong. This should be caught when trying to synthesize
  // the metadata.
  DelegateReader reader(
      [&header](auto offset, auto buffer) {
        StreamContents(
            offset,
            cpp20::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&header), sizeof(header)),
            buffer);
        return fpromise::ok();
      },
      kDefaultImageSize);
  DelegateWriter writer([](auto offset, auto buffer) { return fpromise::ok(); });

  ASSERT_TRUE(FvmImageExtend(reader, options, writer).is_error());
}

fvm::VPartitionEntry MakePartitionEntry(std::string_view name, uint64_t slice_count) {
  fvm::VPartitionEntry entry = {};
  memcpy(entry.unsafe_name, name.data(), name.size());
  memcpy(entry.guid, fvm::kPlaceHolderInstanceGuid.data(), fvm::kPlaceHolderInstanceGuid.size());
  memcpy(entry.type, fvm::kPlaceHolderInstanceGuid.data(), fvm::kPlaceHolderInstanceGuid.size());
  entry.slices = slice_count;
  entry.flags = 0;
  return entry;
}

TEST(FvmImageExtendTest, ValidHeaderAndMetadataWithMissingAllocatedSlicesIsError) {
  const auto options = MakeOptions();
  const auto header = MakeHeader(options, kDefaultSliceCount);
  fvm::VPartitionEntry entry = MakePartitionEntry("partition-1-2-3", 2);

  std::array<fvm::SliceEntry, 2> slices;
  slices[0].Set(1, 0);
  slices[1].Set(1, 1);

  auto metadata_or = fvm::Metadata::Synthesize(header, &entry, 1, slices.data(), slices.size());
  ASSERT_TRUE(metadata_or.is_ok()) << metadata_or.error_value();

  // For this we generate a valid metadata, but reading after the metadata will be an error,
  // out of bounds, this should reveal if there are any errors on reading, that they are handled
  // properly, and not silently continue.
  DelegateReader reader(
      [&metadata_or](auto offset, auto buffer) -> fpromise::result<void, std::string> {
        if (offset + buffer.size() > metadata_or->GetHeader().GetSliceDataOffset(2)) {
          return fpromise::error("Oops no more slices for you!.");
        }
        // Copy whatever chunk of metadata is requested
        StreamContents(offset, AsSpan(metadata_or.value()), buffer);
        return fpromise::ok();
      },
      kDefaultImageSize);
  DelegateWriter writer([](auto offset, auto buffer) { return fpromise::ok(); });

  ASSERT_TRUE(FvmImageExtend(reader, options, writer).is_error());
}

TEST(FvmImageExtendTest, ValidHeaderAndMetadataAndWriterErrorIsError) {
  const auto options = MakeOptions();
  const auto header = MakeHeader(options, kDefaultSliceCount);

  fvm::VPartitionEntry entry = MakePartitionEntry("partition-1-2-3", 2);

  std::array<fvm::SliceEntry, 3> slices;
  slices[0].Set(0, 0);
  slices[1].Set(1, 1);
  slices[1].Set(2, 1);

  auto metadata_or = fvm::Metadata::Synthesize(header, &entry, 1, slices.data(), slices.size());
  ASSERT_TRUE(metadata_or.is_ok()) << metadata_or.error_value();

  DelegateReader reader(
      [&metadata_or](auto offset, auto buffer) -> fpromise::result<void, std::string> {
        StreamContents(offset, AsSpan(metadata_or.value()), buffer);
        return fpromise::ok();
      },
      kDefaultImageSize);
  DelegateWriter writer(
      [](auto offset, auto buffer) { return fpromise::error("Oops I did it again!"); });

  ASSERT_TRUE(FvmImageExtend(reader, options, writer).is_error());
}

fpromise::result<void, std::string> ValidFvmRead(const fvm::Metadata& metadata,
                                                 const FvmOptions& options, uint64_t image_size,
                                                 uint64_t offset,
                                                 cpp20::span<uint8_t> read_buffer) {
  const auto& header = metadata.GetHeader();
  if (offset + read_buffer.size() > image_size) {
    return fpromise::error("ValidFvmRead out of bounds. Offset " + std::to_string(offset) +
                           " Size: " + std::to_string(read_buffer.size()) +
                           " Image Size: " + std::to_string(image_size));
  }

  auto primary_metadata_offset = header.GetSuperblockOffset(fvm::SuperblockType::kPrimary);
  // Reading some chunk from the primary metadata.
  if (offset + read_buffer.size() >= primary_metadata_offset &&
      offset < primary_metadata_offset + header.GetMetadataAllocatedBytes()) {
    uint32_t buffer_offset = 0;
    if (offset < primary_metadata_offset) {
      buffer_offset = primary_metadata_offset - offset;
    }
    StreamContents(offset, AsSpan(metadata), read_buffer.subspan(buffer_offset));
    return fpromise::ok();
  }

  auto secondary_metadata_offset = header.GetSuperblockOffset(fvm::SuperblockType::kPrimary);
  // Reading some chunk from the secondary metadata.
  if (offset + read_buffer.size() >= secondary_metadata_offset &&
      offset < secondary_metadata_offset + header.GetMetadataAllocatedBytes()) {
    uint32_t buffer_offset = 0;
    if (offset < secondary_metadata_offset) {
      buffer_offset = secondary_metadata_offset - offset;
    }
    StreamContents(offset, AsSpan(metadata), read_buffer.subspan(buffer_offset));
    return fpromise::ok();
  }

  // Assumes that physical slices are contiguous on disk. Reads data chunks into the
  // buffer. Also reads at most 1 slice at a time, and for simplification, all reads are
  // within a single slice at this point.
  uint64_t data_offset = header.GetSliceDataOffset(1);
  if (offset + read_buffer.size() >= data_offset &&
      offset < header.GetSliceDataOffset(header.pslice_count + 1)) {
    uint64_t slice_begin = (offset - data_offset) / options.slice_size + 1;
    uint8_t byte_value = static_cast<uint8_t>(slice_begin % std::numeric_limits<uint8_t>::max());
    memset(read_buffer.data(), byte_value, read_buffer.size());
    return fpromise::ok();
  }

  return fpromise::error("ValidFvmRead reading unknown region. Offset " + std::to_string(offset) +
                         " Size: " + std::to_string(read_buffer.size()) +
                         " Image Size: " + std::to_string(image_size));
}

void CheckMetadata(cpp20::span<const fvm::VPartitionEntry> partitions,
                   cpp20::span<const fvm::SliceEntry> slices, const fvm::Metadata& metadata) {
  for (uint64_t vpartition_index = 0; vpartition_index < partitions.size(); ++vpartition_index) {
    const auto& expected_vpartition = partitions[vpartition_index];
    const auto& actual_vpartition = metadata.GetPartitionEntry(vpartition_index + 1);

    EXPECT_EQ(actual_vpartition.name(), expected_vpartition.name());
    EXPECT_TRUE(memcmp(actual_vpartition.type, expected_vpartition.type,
                       sizeof(fvm::VPartitionEntry::type)) == 0);
    EXPECT_TRUE(memcmp(actual_vpartition.guid, expected_vpartition.guid,
                       sizeof(fvm::VPartitionEntry::guid)) == 0);
    EXPECT_EQ(actual_vpartition.slices, expected_vpartition.slices);
    EXPECT_EQ(actual_vpartition.flags, expected_vpartition.flags);
    EXPECT_TRUE(actual_vpartition.IsActive());
  }

  for (uint64_t vpartition_index = partitions.size() + 1;
       vpartition_index < metadata.GetHeader().GetPartitionTableEntryCount(); ++vpartition_index) {
    const auto& unallocated_partition = metadata.GetPartitionEntry(vpartition_index + 1);
    EXPECT_TRUE(!unallocated_partition.IsAllocated());
  }

  for (uint64_t pslice = 0; pslice < slices.size(); ++pslice) {
    const auto& expected_slice = slices[pslice];
    const auto& actual_slice = metadata.GetSliceEntry(pslice + 1);

    EXPECT_EQ(actual_slice.VPartition(), expected_slice.VPartition());
    EXPECT_EQ(actual_slice.VSlice(), expected_slice.VSlice());
    EXPECT_TRUE(actual_slice.IsAllocated());
  }

  for (uint64_t pslice = slices.size() + 1; pslice <= metadata.GetHeader().pslice_count; ++pslice) {
    const auto& unallocated_pslice = metadata.GetSliceEntry(pslice);
    EXPECT_TRUE(!unallocated_pslice.IsAllocated());
  }

  if (testing::Test::HasFailure()) {
    FAIL() << "Metadata check failed";
  }
}

void CheckSliceContents(uint64_t allocated_pslice_count, const FvmOptions& options,
                        const fvm::Metadata& metadata, cpp20::span<const uint8_t> image_contents) {
  // Check each slice content.
  std::vector<uint8_t> expected_slice;
  expected_slice.resize(options.slice_size, 0);
  for (uint64_t pslice = 1; pslice <= allocated_pslice_count; ++pslice) {
    uint64_t slice_offset = metadata.GetHeader().GetSliceDataOffset(pslice);
    uint8_t byte_value = static_cast<uint8_t>(pslice % std::numeric_limits<uint8_t>::max());
    auto actual_slice =
        cpp20::span<const uint8_t>(image_contents).subspan(slice_offset, options.slice_size);

    memset(expected_slice.data(), byte_value, expected_slice.size());

    EXPECT_TRUE(memcmp(actual_slice.data(), expected_slice.data(), actual_slice.size()) == 0);
  }
  if (testing::Test::HasFailure()) {
    FAIL() << "Slice content check failed";
  }
}

fvm::Metadata MakeMetadata(const FvmOptions& options, cpp20::span<uint8_t> image) {
  const auto new_header = MakeHeader(options, kDefaultSliceCount);
  auto primary_metadata = std::make_unique<MetadataBufferView>(
      image.subspan(new_header.GetSuperblockOffset(fvm::SuperblockType::kPrimary),
                    new_header.GetMetadataAllocatedBytes()));

  auto secondary_metadata = std::make_unique<MetadataBufferView>(
      image.subspan(new_header.GetSuperblockOffset(fvm::SuperblockType::kSecondary),
                    new_header.GetMetadataAllocatedBytes()));

  auto new_metadata_or =
      fvm::Metadata::Create(std::move(primary_metadata), std::move(secondary_metadata));

  // No need to check for error, and assertion failure of a present error will fail here.
  return std::move(new_metadata_or.value());
}

TEST(FvmImageExtendTest, ValidFvmImageIsExtendedCorrectly) {
  auto options = MakeOptions();
  const auto header = MakeHeader(options, kDefaultSliceCount);

  // '4 GB image'.
  constexpr uint64_t kImageSize = 4u * (1ull << 32);

  std::vector<fvm::VPartitionEntry> partitions = {MakePartitionEntry("partition-1-2-3", 2),
                                                  MakePartitionEntry("partition-2-3-4", 3)};

  std::array<fvm::SliceEntry, 5> slices;
  slices[0].Set(1, 0);
  slices[1].Set(1, 25);
  slices[2].Set(2, 0);
  slices[3].Set(2, 95);
  slices[4].Set(2, 20);

  auto metadata_or = fvm::Metadata::Synthesize(header, partitions.data(), partitions.size(),
                                               slices.data(), slices.size());
  ASSERT_TRUE(metadata_or.is_ok()) << metadata_or.error_value();

  DelegateReader reader(
      [&metadata_or, &options](auto offset, auto buffer) {
        return ValidFvmRead(metadata_or.value(), options, kImageSize, offset, buffer);
      },
      kDefaultImageSize);

  options.target_volume_size = 2 * options.target_volume_size.value();

  std::vector<uint8_t> fvm_image;
  fvm_image.resize(options.target_volume_size.value(), 0);

  DelegateWriter writer([&fvm_image](auto offset, auto buffer) {
    if (offset + buffer.size() > fvm_image.capacity()) {
      fvm_image.resize(offset + buffer.size(), 0);
    }
    memcpy(&fvm_image[offset], buffer.data(), buffer.size());
    return fpromise::ok();
  });

  auto extend_result = FvmImageExtend(reader, options, writer);
  ASSERT_TRUE(extend_result.is_ok()) << extend_result.error();

  // Verify that the entries and everything is correct.
  auto new_metadata = MakeMetadata(options, fvm_image);

  ASSERT_NO_FATAL_FAILURE(CheckMetadata(partitions, slices, new_metadata));
  ASSERT_NO_FATAL_FAILURE(CheckSliceContents(slices.size(), options, new_metadata, fvm_image));
}

TEST(FvmImageExtendTest, ValidFvmImageWithBigSlicesIsExtendedCorrectly) {
  auto options = MakeOptions();
  // 256 KB slices.
  // For this test to work, we need to pick a slice size, bigger than the max size of the read
  // buffer, which is set at 64 KB.
  options.slice_size = 256 * (1ull << 10);
  const auto header = MakeHeader(options, kDefaultSliceCount);

  // '4 GB image'.
  constexpr uint64_t kImageSize = 4u * (1ull << 32);

  std::vector<fvm::VPartitionEntry> partitions = {MakePartitionEntry("partition-1-2-3", 2),
                                                  MakePartitionEntry("partition-2-3-4", 3)};

  std::array<fvm::SliceEntry, 5> slices;
  slices[0].Set(1, 0);
  slices[1].Set(1, 25);
  slices[2].Set(2, 0);
  slices[3].Set(2, 95);
  slices[4].Set(2, 20);

  auto metadata_or = fvm::Metadata::Synthesize(header, partitions.data(), partitions.size(),
                                               slices.data(), slices.size());
  ASSERT_TRUE(metadata_or.is_ok()) << metadata_or.error_value();

  DelegateReader reader(
      [&metadata_or, &options](auto offset, auto buffer) {
        return ValidFvmRead(metadata_or.value(), options, kImageSize, offset, buffer);
      },
      kDefaultImageSize);

  options.target_volume_size = 2 * options.target_volume_size.value();

  std::vector<uint8_t> fvm_image;
  fvm_image.resize(options.target_volume_size.value(), 0);

  DelegateWriter writer([&fvm_image](auto offset, auto buffer) {
    if (offset + buffer.size() > fvm_image.capacity()) {
      fvm_image.resize(offset + buffer.size(), 0);
    }
    memcpy(&fvm_image[offset], buffer.data(), buffer.size());
    return fpromise::ok();
  });

  auto extend_result = FvmImageExtend(reader, options, writer);
  ASSERT_TRUE(extend_result.is_ok()) << extend_result.error();

  // Verify that the entries and everything is correct.
  auto new_metadata = MakeMetadata(options, fvm_image);

  ASSERT_NO_FATAL_FAILURE(CheckMetadata(partitions, slices, new_metadata));
  ASSERT_NO_FATAL_FAILURE(CheckSliceContents(slices.size(), options, new_metadata, fvm_image));
}

TEST(FvmImageGetTrimmedSizeTest, BadFvmHeaderIsError) {
  DelegateReader reader(
      [](auto offset, auto buffer) {
        memset(buffer.data(), 0, buffer.size());
        return fpromise::ok();
      },
      20u << 20);

  ASSERT_TRUE(FvmImageGetTrimmedSize(reader).is_error());
}

TEST(FvmImageGetTrimmedSizeTest, ValidHeaderWithBadMetadataIsError) {
  const auto options = MakeOptions();
  const auto header = MakeHeader(options, kDefaultSliceCount);

  // This reader just copyes over and over a valid header. This means, that both Superblocks are
  // 'valid', but the rest of the metadata is wrong. This should be caught when trying to synthesize
  // the metadata.
  DelegateReader reader(
      [&header](auto offset, auto buffer) {
        StreamContents(
            offset,
            cpp20::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&header), sizeof(header)),
            buffer);
        return fpromise::ok();
      },
      kDefaultImageSize);

  ASSERT_TRUE(FvmImageGetTrimmedSize(reader).is_error());
}

TEST(FvmImageGetTrimmedSizeTest, TrimmedValueWithNoAllocatedSlicesIsOk) {
  auto options = MakeOptions();
  const auto header = MakeHeader(options, kDefaultSliceCount);
  options.target_volume_size = header.fvm_partition_size;

  // '4 GB image'.
  constexpr uint64_t kImageSize = 4u * (1ull << 32);

  std::vector<fvm::VPartitionEntry> partitions = {MakePartitionEntry("partition-1-2-3", 0),
                                                  MakePartitionEntry("partition-2-3-4", 0)};

  auto metadata_or = fvm::Metadata::Synthesize(header, partitions, cpp20::span<fvm::SliceEntry>());
  ASSERT_TRUE(metadata_or.is_ok()) << metadata_or.error_value();

  DelegateReader reader(
      [&metadata_or, &options](auto offset, auto buffer) {
        return ValidFvmRead(metadata_or.value(), options, kImageSize, offset, buffer);
      },
      kDefaultImageSize);

  auto trim_size_result = FvmImageGetTrimmedSize(reader);
  ASSERT_TRUE(trim_size_result.is_ok()) << trim_size_result.error();

  uint64_t primary_metadata_end = header.GetSuperblockOffset(fvm::SuperblockType::kPrimary) +
                                  header.GetMetadataAllocatedBytes();
  uint64_t secondary_metadata_end = header.GetSuperblockOffset(fvm::SuperblockType::kSecondary) +
                                    header.GetMetadataAllocatedBytes();
  uint64_t metadata_end = std::max(primary_metadata_end, secondary_metadata_end);
  uint64_t last_slice_end = header.GetSliceDataOffset(0);

  // No slices allocated here.
  EXPECT_EQ(trim_size_result.value(), std::max(metadata_end, last_slice_end));
}

TEST(FvmImageGetTrimmedSizeTest, TrimmedValueWithAllocatedSlicesIsOk) {
  auto options = MakeOptions();
  const auto header = MakeHeader(options, kDefaultSliceCount);
  options.target_volume_size = header.fvm_partition_size;

  // '4 GB image'.
  constexpr uint64_t kImageSize = 4u * (1ull << 32);

  std::vector<fvm::VPartitionEntry> partitions = {MakePartitionEntry("partition-1-2-3", 2),
                                                  MakePartitionEntry("partition-2-3-4", 2)};

  std::array<fvm::SliceEntry, 4> slices;
  slices[0].Set(1, 0);
  slices[1].Set(1, 25);
  slices[2].Set(2, 0);
  slices[3].Set(2, 95);

  auto metadata_or = fvm::Metadata::Synthesize(header, partitions, slices);
  ASSERT_TRUE(metadata_or.is_ok()) << metadata_or.error_value();

  DelegateReader reader(
      [&metadata_or, &options](auto offset, auto buffer) {
        return ValidFvmRead(metadata_or.value(), options, kImageSize, offset, buffer);
      },
      kDefaultImageSize);

  auto trim_size_result = FvmImageGetTrimmedSize(reader);
  ASSERT_TRUE(trim_size_result.is_ok()) << trim_size_result.error();

  uint64_t primary_metadata_end = header.GetSuperblockOffset(fvm::SuperblockType::kPrimary) +
                                  header.GetMetadataAllocatedBytes();
  uint64_t secondary_metadata_end = header.GetSuperblockOffset(fvm::SuperblockType::kSecondary) +
                                    header.GetMetadataAllocatedBytes();
  uint64_t metadata_end = std::max(primary_metadata_end, secondary_metadata_end);
  // Pslice are 1 indexed, plus we account for the data in the slice itself.
  uint64_t last_slice_end = header.GetSliceDataOffset(5);

  // No slices allocated here.
  EXPECT_EQ(trim_size_result.value(), std::max(metadata_end, last_slice_end));
}

TEST(FvmImageGetSizeTest, ValidHeaderWithBadMetadataIsError) {
  const auto options = MakeOptions();
  const auto header = MakeHeader(options, kDefaultSliceCount);

  // This reader just copyes over and over a valid header. This means, that both Superblocks are
  // 'valid', but the rest of the metadata is wrong. This should be caught when trying to synthesize
  // the metadata.
  DelegateReader reader(
      [&header](auto offset, auto buffer) {
        StreamContents(
            offset,
            cpp20::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&header), sizeof(header)),
            buffer);
        return fpromise::ok();
      },
      kDefaultImageSize);

  ASSERT_TRUE(FvmImageGetSize(reader).is_error());
}

TEST(FvmImageGetSize, ReturnsFvmPartitionSizeFromHeader) {
  auto options = MakeOptions();
  const auto header = MakeHeader(options, kDefaultSliceCount);
  options.target_volume_size = header.fvm_partition_size;

  // '4 GB image'.
  constexpr uint64_t kImageSize = 4u * (1ull << 32);

  std::vector<fvm::VPartitionEntry> partitions = {MakePartitionEntry("partition-1-2-3", 0),
                                                  MakePartitionEntry("partition-2-3-4", 0)};

  auto metadata_or = fvm::Metadata::Synthesize(header, partitions, cpp20::span<fvm::SliceEntry>());
  ASSERT_TRUE(metadata_or.is_ok()) << metadata_or.error_value();

  DelegateReader reader(
      [&metadata_or, &options](auto offset, auto buffer) {
        return ValidFvmRead(metadata_or.value(), options, kImageSize, offset, buffer);
      },
      kDefaultImageSize);
  auto size_or = FvmImageGetSize(reader);

  ASSERT_TRUE(size_or.is_ok()) << size_or.error();
  EXPECT_EQ(size_or.value(), header.fvm_partition_size);
}

}  // namespace
}  // namespace storage::volume_image
