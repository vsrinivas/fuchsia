// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/fvm/fvm_sparse_image.h"

#include <lib/fit/function.h>

#include <cstdint>
#include <limits>
#include <memory>

#include <fvm/fvm-sparse.h>
#include <fvm/sparse-reader.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/storage/volume_image/fvm/address_descriptor.h"
#include "src/storage/volume_image/fvm/fvm_descriptor.h"
#include "src/storage/volume_image/fvm/options.h"
#include "src/storage/volume_image/options.h"
#include "src/storage/volume_image/utils/reader.h"
#include "src/storage/volume_image/utils/writer.h"
#include "src/storage/volume_image/volume_descriptor.h"

namespace storage::volume_image {
namespace {

TEST(FvmSparseImageTest, GetImageFlagsMapsLz4CompressionCorrectly) {
  FvmOptions options;
  options.compression.schema = CompressionSchema::kLz4;

  auto flag = fvm_sparse_internal::GetImageFlags(options);
  EXPECT_EQ(fvm::kSparseFlagLz4, flag & fvm::kSparseFlagLz4);
}

TEST(FvmSparseImageTest, GetImageFlagsMapsNoCompressionCorrectly) {
  FvmOptions options;
  options.compression.schema = CompressionSchema::kNone;

  auto flag = fvm_sparse_internal::GetImageFlags(options);
  EXPECT_EQ(0u, flag);
}

TEST(FvmSparseImageTest, GetImageFlagsMapsUnknownCompressionCorrectly) {
  FvmOptions options;
  options.compression.schema = static_cast<CompressionSchema>(-1);

  auto flag = fvm_sparse_internal::GetImageFlags(options);
  EXPECT_EQ(0u, flag);
}

TEST(FvmSparseImageTest, GetPartitionFlagMapsEncryptionCorrectly) {
  VolumeDescriptor descriptor;
  descriptor.encryption = EncryptionType::kZxcrypt;
  AddressDescriptor address;
  Partition partition(descriptor, address, nullptr);

  auto flag = fvm_sparse_internal::GetPartitionFlags(partition);
  EXPECT_EQ(fvm::kSparseFlagZxcrypt, flag & fvm::kSparseFlagZxcrypt);
}

TEST(FvmSparseImageTest, GetPartitionFlagMapsNoEncryptionCorrectly) {
  VolumeDescriptor descriptor = {};
  descriptor.encryption = EncryptionType::kNone;
  AddressDescriptor address = {};
  Partition partition(descriptor, address, nullptr);

  auto flag = fvm_sparse_internal::GetPartitionFlags(partition);
  EXPECT_EQ(0u, flag);
}

TEST(FvmSparseImageTest, GetPartitionFlagMapsUnknownEncryptionCorrectly) {
  VolumeDescriptor descriptor = {};
  descriptor.encryption = static_cast<EncryptionType>(-1);
  AddressDescriptor address = {};
  Partition partition(descriptor, address, nullptr);

  auto flag = fvm_sparse_internal::GetPartitionFlags(partition);
  EXPECT_EQ(0u, flag);
}

static constexpr std::string_view kSerializedVolumeImage1 = R"(
{
    "volume": {
      "magic": 11602964,
      "instance_guid": "04030201-0605-0807-1009-111213141516",
      "type_guid": "A4A3A2A1-B6B5-C8C7-D0D1-E0E1E2E3E4E5",
      "name": "partition-1",
      "block_size": 16,
      "encryption_type": "ENCRYPTION_TYPE_ZXCRYPT",
      "options" : [
        "OPTION_NONE",
        "OPTION_EMPTY"
      ]
    },
    "address": {
        "magic": 12526821592682033285,
        "mappings": [
          {
            "source": 20,
            "target": 400,
            "count": 10
          },
          {
            "source": 180,
            "target": 160,
            "count": 5
          },
          {
            "source": 190,
            "target": 170,
            "count": 1
          }
        ]
    }
})";

static constexpr std::string_view kSerializedVolumeImage2 = R"(
{
    "volume": {
      "magic": 11602964,
      "instance_guid": "04030201-0605-0807-1009-111213141517",
      "type_guid": "A4A3A2A1-B6B5-C8C7-D0D1-E0E1E2E3E4E6",
      "name": "partition-2",
      "block_size": 32,
      "encryption_type": "ENCRYPTION_TYPE_ZXCRYPT",
      "options" : [
        "OPTION_NONE",
        "OPTION_EMPTY"
      ]
    },
    "address": {
        "magic": 12526821592682033285,
        "mappings": [
          {
            "source": 25,
            "target": 150,
            "count": 1
          },
          {
            "source": 250,
            "target": 320,
            "count": 2
          }
        ]
    }
})";

// This struct represents a typed version of how the serialized contents of
// |SerializedVolumeImage1| and |SerializedVolumeImage2| would look.
struct SerializedSparseImage {
  fvm::sparse_image_t header;
  struct {
    fvm::partition_descriptor_t descriptor;
    fvm::extent_descriptor_t extents[3];
  } partition_1 __attribute__((packed));
  struct {
    fvm::partition_descriptor_t descriptor;
    fvm::extent_descriptor_t extents[2];
  } partition_2 __attribute__((packed));
  uint8_t extent_1[160];
  uint8_t extent_2[80];
  uint8_t extent_3[16];
  uint8_t extent_4[32];
  uint8_t extent_5[64];
} __attribute__((packed));

FvmDescriptor MakeDescriptor() {
  FvmOptions options;
  options.compression.schema = CompressionSchema::kLz4;
  options.max_volume_size = 20 * (1 << 20);
  options.slice_size = 64;

  auto partition_1_result = Partition::Create(kSerializedVolumeImage1, nullptr);
  EXPECT_TRUE(partition_1_result.is_ok()) << partition_1_result.error();
  auto partition_2_result = Partition::Create(kSerializedVolumeImage2, nullptr);
  EXPECT_TRUE(partition_2_result.is_ok()) << partition_2_result.error();

  auto descriptor_result = FvmDescriptor::Builder()
                               .SetOptions(options)
                               .AddPartition(partition_1_result.take_value())
                               .AddPartition(partition_2_result.take_value())
                               .Build();
  EXPECT_TRUE(descriptor_result.is_ok()) << descriptor_result.error();
  return descriptor_result.take_value();
}

TEST(FvmSparseImageTest, FvmSparseGenerateHeaderMatchersFvmDescriptor) {
  FvmDescriptor descriptor = MakeDescriptor();
  auto header = FvmSparseGenerateHeader(descriptor);

  EXPECT_EQ(descriptor.partitions().size(), header.partition_count);
  EXPECT_EQ(descriptor.options().max_volume_size.value(), header.maximum_disk_size);
  EXPECT_EQ(descriptor.options().slice_size, header.slice_size);
  EXPECT_EQ(fvm::kSparseFormatMagic, header.magic);
  EXPECT_EQ(fvm::kSparseFormatVersion, header.version);
  EXPECT_EQ(fvm_sparse_internal::GetImageFlags(descriptor.options()), header.flags);

  uint64_t extent_count = 0;
  for (const auto& partition : descriptor.partitions()) {
    extent_count += partition.address().mappings.size();
  }
  uint64_t expected_header_length = sizeof(fvm::sparse_image_t) +
                                    sizeof(fvm::partition_descriptor_t) * header.partition_count +
                                    sizeof(fvm::extent_descriptor_t) * extent_count;
  EXPECT_EQ(expected_header_length, header.header_length);
}

TEST(FvmSparseImageTest, FvmSparGeneratePartitionEntryMatchesPartition) {
  FvmDescriptor descriptor = MakeDescriptor();
  const auto& partition = *descriptor.partitions().begin();

  auto partition_entry =
      FvmSparseGeneratePartitionEntry(descriptor.options().slice_size, partition);

  EXPECT_EQ(fvm::kPartitionDescriptorMagic, partition_entry.descriptor.magic);
  EXPECT_TRUE(memcmp(partition.volume().type.data(), partition_entry.descriptor.type,
                     partition.volume().type.size()) == 0);
  EXPECT_EQ(fvm_sparse_internal::GetPartitionFlags(partition), partition_entry.descriptor.flags);
  EXPECT_STREQ(partition.volume().name.data(),
               reinterpret_cast<const char*>(partition_entry.descriptor.name));
  EXPECT_EQ(partition.address().mappings.size(), partition_entry.descriptor.extent_count);
}

TEST(FvmSparseImageTest, FvmSparseCalculateImageSizeForEmptyDescriptorIsHeaderSize) {
  FvmDescriptor descriptor;
  EXPECT_EQ(sizeof(fvm::sparse_image_t), FvmSparseCalculateImageSize(descriptor));
}

TEST(FvmSparseImageTest,
     FvmSparseCalculateImageSizeWithParitionsAndExtentsMatchesSerializedContent) {
  FvmDescriptor descriptor = MakeDescriptor();
  uint64_t header_length = FvmSparseGenerateHeader(descriptor).header_length;
  uint64_t data_length = 0;
  for (const auto& partition : descriptor.partitions()) {
    for (const auto& mapping : partition.address().mappings) {
      data_length += mapping.count * partition.volume().block_size;
    }
  }

  EXPECT_EQ(header_length + data_length, FvmSparseCalculateImageSize(descriptor));
}

// Fake implementation for reader that delegates operations to a function after performing bound
// check.
class FakeReader : public Reader {
 public:
  explicit FakeReader(fit::function<std::string(uint64_t, fbl::Span<uint8_t>)> filler)
      : filler_(std::move(filler)) {}

  std::string Read(uint64_t offset, fbl::Span<uint8_t> buffer) const final {
    return filler_(offset, buffer);
  }

 private:
  fit::function<std::string(uint64_t offset, fbl::Span<uint8_t>)> filler_;
};

// Fake writer implementations that writes into a provided buffer.
class BufferWriter : public Writer {
 public:
  explicit BufferWriter(fbl::Span<uint8_t> buffer) : buffer_(buffer) {}

  std::string Write(uint64_t offset, fbl::Span<const uint8_t> buffer) final {
    if (offset > buffer_.size() || offset + buffer.size() > buffer_.size()) {
      return "Out of Range";
    }
    memcpy(buffer_.data() + offset, buffer.data(), buffer.size());
    return "";
  }

 private:
  fbl::Span<uint8_t> buffer_;
};

template <int shift>
std::string GetContents(uint64_t offset, fbl::Span<uint8_t> buffer) {
  for (uint64_t index = 0; index < buffer.size(); ++index) {
    buffer[index] = (offset + index + shift) % sizeof(uint64_t);
  }
  return "";
}

template <typename T, size_t size>
std::string_view ToStringView(const T (&a)[size]) {
  std::string_view view(reinterpret_cast<const char*>(a), size);
  return view.substr(0, view.rfind('\0'));
}

void PartitionsAreEqual(const fvm::partition_descriptor_t& lhs,
                        const fvm::partition_descriptor_t& rhs) {
  EXPECT_EQ(lhs.magic, rhs.magic);
  EXPECT_EQ(ToStringView(lhs.name), ToStringView(rhs.name));
  EXPECT_TRUE(memcmp(lhs.type, rhs.type, sizeof(fvm::partition_descriptor_t::type)) == 0);
  EXPECT_EQ(lhs.flags, rhs.flags);
  EXPECT_EQ(lhs.extent_count, rhs.extent_count);
  ASSERT_FALSE(testing::Test::HasFailure());
}

TEST(FvmSparseImageTest, FvmSparseWriteImageDataUncompressedCompliesWithFormat) {
  auto serialized_sparse_image = std::make_unique<SerializedSparseImage>();
  auto buffer = fbl::Span<uint8_t>(reinterpret_cast<uint8_t*>(serialized_sparse_image.get()),
                                   sizeof(SerializedSparseImage));
  BufferWriter writer(buffer);

  FvmOptions options;
  options.compression.schema = CompressionSchema::kNone;
  options.max_volume_size = 20 * (1 << 20);
  options.slice_size = 8192;

  auto partition_1_result =
      Partition::Create(kSerializedVolumeImage1, std::make_unique<FakeReader>(GetContents<1>));
  ASSERT_TRUE(partition_1_result.is_ok()) << partition_1_result.error();

  auto partition_2_result =
      Partition::Create(kSerializedVolumeImage2, std::make_unique<FakeReader>(GetContents<2>));
  ASSERT_TRUE(partition_2_result.is_ok()) << partition_2_result.error();

  FvmDescriptor::Builder builder;
  auto result = builder.SetOptions(options)
                    .AddPartition(partition_2_result.take_value())
                    .AddPartition(partition_1_result.take_value())
                    .Build();
  ASSERT_TRUE(result.is_ok()) << result.error();
  auto descriptor = result.take_value();

  auto header = FvmSparseGenerateHeader(descriptor);
  std::vector<FvmSparsePartitionEntry> partitions;
  for (const auto& partition : descriptor.partitions()) {
    partitions.push_back(FvmSparseGeneratePartitionEntry(options.slice_size, partition));
  }

  auto write_result = FvmSparseWriteImage(descriptor, &writer);
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();

  EXPECT_TRUE(memcmp(&serialized_sparse_image->header, &header, sizeof(fvm::sparse_image_t)) == 0);

  // Check partition and entry descriptors.
  auto it = descriptor.partitions().begin();
  const auto& partition_1 = *it++;
  auto partition_1_entry = FvmSparseGeneratePartitionEntry(options.slice_size, partition_1);
  ASSERT_NO_FATAL_FAILURE(PartitionsAreEqual(serialized_sparse_image->partition_1.descriptor,
                                             partition_1_entry.descriptor));
  EXPECT_TRUE(memcmp(&serialized_sparse_image->partition_1.extents[0],
                     &partition_1_entry.extents[0], sizeof(fvm::extent_descriptor_t)) == 0);
  EXPECT_TRUE(memcmp(&serialized_sparse_image->partition_1.extents[1],
                     &partition_1_entry.extents[1], sizeof(fvm::extent_descriptor_t)) == 0);
  EXPECT_TRUE(memcmp(&serialized_sparse_image->partition_1.extents[2],
                     &partition_1_entry.extents[2], sizeof(fvm::extent_descriptor_t)) == 0);

  const auto& partition_2 = *it++;
  auto partition_2_entry = FvmSparseGeneratePartitionEntry(options.slice_size, partition_2);
  ASSERT_NO_FATAL_FAILURE(PartitionsAreEqual(serialized_sparse_image->partition_2.descriptor,
                                             partition_2_entry.descriptor));
  EXPECT_TRUE(memcmp(&serialized_sparse_image->partition_2.extents[0],
                     &partition_2_entry.extents[0], sizeof(fvm::extent_descriptor_t)) == 0);
  EXPECT_TRUE(memcmp(&serialized_sparse_image->partition_2.extents[1],
                     &partition_2_entry.extents[1], sizeof(fvm::extent_descriptor_t)) == 0);

  // Check data is correct.
  uint64_t partition_index = 0;
  const uint8_t* extents[2][3] = {
      {serialized_sparse_image->extent_1, serialized_sparse_image->extent_2,
       serialized_sparse_image->extent_3},
      {serialized_sparse_image->extent_4, serialized_sparse_image->extent_5}};
  for (const auto& partition : descriptor.partitions()) {
    auto read_content = partition_index == 0 ? GetContents<1> : GetContents<2>;
    std::vector<uint8_t> extent_content;
    uint64_t extent_index = 0;
    for (const auto& mapping : partition.address().mappings) {
      extent_content.resize(mapping.count * partition.volume().block_size, 0);
      ASSERT_TRUE(
          read_content(mapping.source * partition.volume().block_size, extent_content).empty());
      EXPECT_TRUE(memcmp(extents[partition_index][extent_index], extent_content.data(),
                         extent_content.size()) == 0);
    }
  }
}

class ErrorWriter final : public Writer {
 public:
  ErrorWriter(uint64_t error_offset, std::string_view error)
      : error_(error), error_offset_(error_offset) {}
  ~ErrorWriter() final = default;

  std::string Write([[maybe_unused]] uint64_t offset,
                    [[maybe_unused]] fbl::Span<const uint8_t> buffer) final {
    if (offset >= error_offset_) {
      return error_;
    }
    return "";
  }

 private:
  std::string error_;
  uint64_t error_offset_;
};

constexpr std::string_view kWriteError = "Write Error";
constexpr std::string_view kReadError = "Read Error";

TEST(FvmSparseImageTest, FvmSparseWriteImageWithReadErrorIsError) {
  FvmOptions options;
  options.compression.schema = CompressionSchema::kNone;
  options.max_volume_size = 20 * (1 << 20);
  options.slice_size = 8192;

  auto partition_1_result = Partition::Create(
      kSerializedVolumeImage1,
      std::make_unique<FakeReader>(
          []([[maybe_unused]] uint64_t offset, [[maybe_unused]] fbl::Span<uint8_t> buffer) {
            return std::string(kReadError);
          }));
  ASSERT_TRUE(partition_1_result.is_ok()) << partition_1_result.error();

  FvmDescriptor::Builder builder;
  auto result = builder.SetOptions(options).AddPartition(partition_1_result.take_value()).Build();
  ASSERT_TRUE(result.is_ok()) << result.error();
  auto descriptor = result.take_value();

  // We only added a single partition, so, data should be at this offset.
  ErrorWriter writer(/**error_offset=**/ offsetof(SerializedSparseImage, partition_2), kWriteError);
  auto write_result = FvmSparseWriteImage(descriptor, &writer);
  ASSERT_TRUE(write_result.is_error());
  ASSERT_EQ(kReadError, write_result.error());
}

TEST(FvmSparseImageTest, FvmSparseWriteImageWithWriteErrorIsError) {
  ErrorWriter writer(/**error_offset=**/ 0, kWriteError);
  FvmOptions options;
  options.compression.schema = CompressionSchema::kNone;
  options.max_volume_size = 20 * (1 << 20);
  options.slice_size = 8192;

  auto partition_1_result =
      Partition::Create(kSerializedVolumeImage1, std::make_unique<FakeReader>(&GetContents<0>));
  ASSERT_TRUE(partition_1_result.is_ok()) << partition_1_result.error();

  FvmDescriptor::Builder builder;
  auto result = builder.SetOptions(options).AddPartition(partition_1_result.take_value()).Build();
  ASSERT_TRUE(result.is_ok()) << result.error();
  auto descriptor = result.take_value();

  auto write_result = FvmSparseWriteImage(descriptor, &writer);
  ASSERT_TRUE(write_result.is_error());
  ASSERT_EQ(kWriteError, write_result.error());
}

class FvmSparseReaderImpl final : public fvm::ReaderInterface {
 public:
  explicit FvmSparseReaderImpl(fbl::Span<const uint8_t> buffer) : buffer_(buffer) {}

  ~FvmSparseReaderImpl() final = default;

  zx_status_t Read(void* buf, size_t buf_size, size_t* size_actual) final {
    size_t bytes_to_read = std::min(buf_size, buffer_.size() - cursor_);
    memcpy(buf, buffer_.data() + cursor_, bytes_to_read);
    *size_actual = bytes_to_read;
    cursor_ += bytes_to_read;
    return ZX_OK;
  }

 private:
  fbl::Span<const uint8_t> buffer_;
  size_t cursor_ = 0;
};

TEST(FvmSparseImageTest, SparseReaderIsAbleToParseSerializedData) {
  auto serialized_sparse_image = std::make_unique<SerializedSparseImage>();
  auto buffer = fbl::Span<uint8_t>(reinterpret_cast<uint8_t*>(serialized_sparse_image.get()),
                                   sizeof(SerializedSparseImage));
  BufferWriter writer(buffer);

  FvmOptions options;
  options.compression.schema = CompressionSchema::kNone;
  options.max_volume_size = 20 * (1 << 20);
  options.slice_size = 8192;

  auto partition_1_result =
      Partition::Create(kSerializedVolumeImage1, std::make_unique<FakeReader>(GetContents<1>));
  ASSERT_TRUE(partition_1_result.is_ok()) << partition_1_result.error();

  auto partition_2_result =
      Partition::Create(kSerializedVolumeImage2, std::make_unique<FakeReader>(GetContents<2>));
  ASSERT_TRUE(partition_2_result.is_ok()) << partition_2_result.error();

  FvmDescriptor::Builder builder;
  auto result = builder.SetOptions(options)
                    .AddPartition(partition_2_result.take_value())
                    .AddPartition(partition_1_result.take_value())
                    .Build();
  ASSERT_TRUE(result.is_ok()) << result.error();
  auto descriptor = result.take_value();

  auto write_result = FvmSparseWriteImage(descriptor, &writer);
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();

  std::unique_ptr<FvmSparseReaderImpl> sparse_reader_impl(new FvmSparseReaderImpl(buffer));
  std::unique_ptr<fvm::SparseReader> sparse_reader = nullptr;
  // This verifies metadata(header, partition descriptors and extent descriptors.)
  ASSERT_EQ(ZX_OK, fvm::SparseReader::Create(std::move(sparse_reader_impl), &sparse_reader));

  // Verify that data is read accordingly.
  uint64_t partition_index = 0;
  const uint8_t* extents[2][3] = {
      {serialized_sparse_image->extent_1, serialized_sparse_image->extent_2,
       serialized_sparse_image->extent_3},
      {serialized_sparse_image->extent_4, serialized_sparse_image->extent_5}};
  for (const auto& partition : descriptor.partitions()) {
    std::vector<uint8_t> extent_content;
    uint64_t extent_index = 0;
    // Check that extents match each other.
    for (const auto& mapping : partition.address().mappings) {
      extent_content.resize(mapping.count * partition.volume().block_size, 0);
      size_t read_bytes = 0;
      ASSERT_EQ(ZX_OK,
                sparse_reader->ReadData(extent_content.data(), extent_content.size(), &read_bytes));
      ASSERT_EQ(extent_content.size(), read_bytes);
      EXPECT_TRUE(memcmp(extents[partition_index][extent_index], extent_content.data(),
                         extent_content.size()) == 0);
      extent_index++;
    }
    partition_index++;
  }
}

}  // namespace
}  // namespace storage::volume_image
