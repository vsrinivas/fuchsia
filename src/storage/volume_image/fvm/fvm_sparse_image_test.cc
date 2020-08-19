// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/fvm/fvm_sparse_image.h"

#include <lib/fit/function.h>

#include <cstdint>
#include <limits>
#include <memory>

#include <fbl/auto_call.h>
#include <fvm/fvm-sparse.h>
#include <fvm/sparse-reader.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/storage/volume_image/address_descriptor.h"
#include "src/storage/volume_image/fvm/fvm_descriptor.h"
#include "src/storage/volume_image/fvm/options.h"
#include "src/storage/volume_image/options.h"
#include "src/storage/volume_image/utils/lz4_compressor.h"
#include "src/storage/volume_image/utils/reader.h"
#include "src/storage/volume_image/utils/writer.h"
#include "src/storage/volume_image/volume_descriptor.h"

namespace storage::volume_image {
namespace {

TEST(FvmSparseImageTest, GetImageFlagsMapsLz4CompressionCorrectly) {
  FvmOptions options;
  options.compression.schema = CompressionSchema::kLz4;

  auto flag = fvm_sparse_internal::GetImageFlags(options);
  EXPECT_EQ(flag & fvm::kSparseFlagLz4, fvm::kSparseFlagLz4);
}

TEST(FvmSparseImageTest, GetImageFlagsMapsNoCompressionCorrectly) {
  FvmOptions options;
  options.compression.schema = CompressionSchema::kNone;

  auto flag = fvm_sparse_internal::GetImageFlags(options);
  EXPECT_EQ(flag, 0u);
}

TEST(FvmSparseImageTest, GetImageFlagsMapsUnknownCompressionCorrectly) {
  FvmOptions options;
  options.compression.schema = static_cast<CompressionSchema>(-1);

  auto flag = fvm_sparse_internal::GetImageFlags(options);
  EXPECT_EQ(flag, 0u);
}

TEST(FvmSparseImageTest, GetPartitionFlagMapsEncryptionCorrectly) {
  VolumeDescriptor descriptor;
  descriptor.encryption = EncryptionType::kZxcrypt;
  AddressDescriptor address;
  Partition partition(descriptor, address, nullptr);

  auto flag = fvm_sparse_internal::GetPartitionFlags(partition);
  EXPECT_EQ(flag & fvm::kSparseFlagZxcrypt, fvm::kSparseFlagZxcrypt);
}

TEST(FvmSparseImageTest, GetPartitionFlagMapsNoEncryptionCorrectly) {
  VolumeDescriptor descriptor = {};
  descriptor.encryption = EncryptionType::kNone;
  AddressDescriptor address = {};
  Partition partition(descriptor, address, nullptr);

  auto flag = fvm_sparse_internal::GetPartitionFlags(partition);
  EXPECT_EQ(flag, 0u);
}

TEST(FvmSparseImageTest, GetPartitionFlagMapsUnknownEncryptionCorrectly) {
  VolumeDescriptor descriptor = {};
  descriptor.encryption = static_cast<EncryptionType>(-1);
  AddressDescriptor address = {};
  Partition partition(descriptor, address, nullptr);

  auto flag = fvm_sparse_internal::GetPartitionFlags(partition);
  EXPECT_EQ(flag, 0u);
}

constexpr std::string_view kSerializedVolumeImage1 = R"(
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
            "target": 8192,
            "count": 48
          },
          {
            "source": 180,
            "target": 0,
            "count": 52
          },
          {
            "source": 190,
            "target": 16384,
            "count": 20
          }
        ]
    }
})";

constexpr std::string_view kSerializedVolumeImage2 = R"(
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
            "target": 0,
            "count": 30
          },
          {
            "source": 250,
            "target": 327680,
            "count": 61
          }
        ]
    }
})";

// This struct represents a typed version of how the serialized contents of
// |SerializedVolumeImage1| and |SerializedVolumeImage2| would look.
struct SerializedSparseImage {
  fvm::SparseImage header;
  struct {
    fvm::PartitionDescriptor descriptor;
    fvm::ExtentDescriptor extents[3];
  } partition_1 __attribute__((packed));
  struct {
    fvm::PartitionDescriptor descriptor;
    fvm::ExtentDescriptor extents[2];
  } partition_2 __attribute__((packed));
  uint8_t extent_data[211];
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

  EXPECT_EQ(header.partition_count, descriptor.partitions().size());
  EXPECT_EQ(header.maximum_disk_size, descriptor.options().max_volume_size.value());
  EXPECT_EQ(descriptor.options().slice_size, header.slice_size);
  EXPECT_EQ(header.magic, fvm::kSparseFormatMagic);
  EXPECT_EQ(header.version, fvm::kSparseFormatVersion);
  EXPECT_EQ(header.flags, fvm_sparse_internal::GetImageFlags(descriptor.options()));

  uint64_t extent_count = 0;
  for (const auto& partition : descriptor.partitions()) {
    extent_count += partition.address().mappings.size();
  }
  uint64_t expected_header_length = sizeof(fvm::SparseImage) +
                                    sizeof(fvm::PartitionDescriptor) * header.partition_count +
                                    sizeof(fvm::ExtentDescriptor) * extent_count;
  EXPECT_EQ(header.header_length, expected_header_length);
}

TEST(FvmSparseImageTest, FvmSparGeneratePartitionEntryMatchesPartition) {
  FvmDescriptor descriptor = MakeDescriptor();
  const auto& partition = *descriptor.partitions().begin();

  auto partition_entry_result =
      FvmSparseGeneratePartitionEntry(descriptor.options().slice_size, partition);
  ASSERT_TRUE(partition_entry_result.is_ok()) << partition_entry_result.error();
  auto partition_entry = partition_entry_result.take_value();

  EXPECT_EQ(fvm::kPartitionDescriptorMagic, partition_entry.descriptor.magic);
  EXPECT_TRUE(memcmp(partition.volume().type.data(), partition_entry.descriptor.type,
                     partition.volume().type.size()) == 0);
  EXPECT_TRUE(memcmp(partition.volume().name.data(), partition_entry.descriptor.name,
                     partition.volume().name.size()) == 0);
  EXPECT_EQ(partition_entry.descriptor.flags, fvm_sparse_internal::GetPartitionFlags(partition));
  EXPECT_EQ(partition.address().mappings.size(), partition_entry.descriptor.extent_count);
}

TEST(FvmSparseImageTest, FvmSparseCalculateUncompressedImageSizeForEmptyDescriptorIsHeaderSize) {
  FvmDescriptor descriptor;
  EXPECT_EQ(sizeof(fvm::SparseImage), FvmSparseCalculateUncompressedImageSize(descriptor));
}

TEST(FvmSparseImageTest,
     FvmSparseCalculateUncompressedImageSizeWithParitionsAndExtentsMatchesSerializedContent) {
  FvmDescriptor descriptor = MakeDescriptor();
  uint64_t header_length = FvmSparseGenerateHeader(descriptor).header_length;
  uint64_t data_length = 0;
  for (const auto& partition : descriptor.partitions()) {
    for (const auto& mapping : partition.address().mappings) {
      data_length += mapping.count;
    }
  }

  EXPECT_EQ(FvmSparseCalculateUncompressedImageSize(descriptor), header_length + data_length);
}

// Fake implementation for reader that delegates operations to a function after performing bound
// check.
class FakeReader : public Reader {
 public:
  explicit FakeReader(
      fit::function<fit::result<void, std::string>(uint64_t, fbl::Span<uint8_t>)> filler)
      : filler_(std::move(filler)) {}

  uint64_t GetMaximumOffset() const override { return 0; }

  fit::result<void, std::string> Read(uint64_t offset, fbl::Span<uint8_t> buffer) const final {
    return filler_(offset, buffer);
  }

 private:
  fit::function<fit::result<void, std::string>(uint64_t offset, fbl::Span<uint8_t>)> filler_;
};

// Fake writer implementations that writes into a provided buffer.
class BufferWriter : public Writer {
 public:
  explicit BufferWriter(fbl::Span<uint8_t> buffer) : buffer_(buffer) {}

  fit::result<void, std::string> Write(uint64_t offset, fbl::Span<const uint8_t> buffer) final {
    if (offset > buffer_.size() || offset + buffer.size() > buffer_.size()) {
      return fit::error("Out of Range");
    }
    memcpy(buffer_.data() + offset, buffer.data(), buffer.size());
    return fit::ok();
  }

 private:
  fbl::Span<uint8_t> buffer_;
};

template <int shift>
fit::result<void, std::string> GetContents(uint64_t offset, fbl::Span<uint8_t> buffer) {
  for (uint64_t index = 0; index < buffer.size(); ++index) {
    buffer[index] = (offset + index + shift) % sizeof(uint64_t);
  }
  return fit::ok();
}

class SerializedImageContainer {
 public:
  SerializedImageContainer() : serialized_image_(new SerializedSparseImage()), writer_(AsSpan()) {}

  fbl::Span<uint8_t> AsSpan() {
    return fbl::Span<uint8_t>(reinterpret_cast<uint8_t*>(serialized_image_.get()),
                              sizeof(SerializedSparseImage));
  }

  const SerializedSparseImage& serialized_image() const { return *serialized_image_; }

  SerializedSparseImage& serialized_image() { return *serialized_image_; }

  BufferWriter& writer() { return writer_; }

  std::vector<fbl::Span<const uint8_t>> PartitionExtents(size_t index) {
    auto view = fbl::Span(serialized_image_->extent_data);
    if (index == 0) {
      return {{view.subspan(0, 48), view.subspan(48, 52), view.subspan(100, 20)}};
    }
    return {{view.subspan(120, 30), view.subspan(150, 61)}};
  }

 private:
  std::unique_ptr<SerializedSparseImage> serialized_image_ = nullptr;
  BufferWriter writer_;
};

FvmDescriptor MakeDescriptorWithOptions(const FvmOptions& options) {
  auto partition_1_result =
      Partition::Create(kSerializedVolumeImage1, std::make_unique<FakeReader>(GetContents<1>));

  auto partition_2_result =
      Partition::Create(kSerializedVolumeImage2, std::make_unique<FakeReader>(GetContents<2>));

  FvmDescriptor::Builder builder;
  auto result = builder.SetOptions(options)
                    .AddPartition(partition_2_result.take_value())
                    .AddPartition(partition_1_result.take_value())
                    .Build();
  return result.take_value();
}

FvmOptions MakeOptions(uint64_t slice_size, CompressionSchema schema) {
  FvmOptions options;
  options.compression.schema = schema;
  options.max_volume_size = 20 * (1 << 20);
  options.slice_size = slice_size;

  return options;
}

std::vector<FvmSparsePartitionEntry> GetExpectedPartitionEntries(const FvmDescriptor& descriptor,
                                                                 uint64_t slice_size) {
  std::vector<FvmSparsePartitionEntry> partitions;
  for (const auto& partition : descriptor.partitions()) {
    auto partition_entry_result = FvmSparseGeneratePartitionEntry(slice_size, partition);
    partitions.push_back(partition_entry_result.take_value());
  }
  return partitions;
}

auto HeaderEq(const fvm::SparseImage& expected_header) {
  using Header = fvm::SparseImage;
  return testing::AllOf(
      testing::Field(&Header::header_length, testing::Eq(expected_header.header_length)),
      testing::Field(&Header::flags, testing::Eq(expected_header.flags)),
      testing::Field(&Header::magic, testing::Eq(expected_header.magic)),
      testing::Field(&Header::partition_count, testing::Eq(expected_header.partition_count)),
      testing::Field(&Header::slice_size, testing::Eq(expected_header.slice_size)),
      testing::Field(&Header::maximum_disk_size, testing::Eq(expected_header.maximum_disk_size)),
      testing::Field(&Header::version, testing::Eq(expected_header.version)));
}

auto PartitionDescriptorEq(const fvm::PartitionDescriptor& expected_descriptor) {
  using PartitionDescriptor = fvm::PartitionDescriptor;
  return testing::AllOf(
      testing::Field(&PartitionDescriptor::magic, testing::Eq(expected_descriptor.magic)),
      testing::Field(&PartitionDescriptor::flags, testing::Eq(expected_descriptor.flags)),
      testing::Field(&PartitionDescriptor::name,
                     testing::ElementsAreArray(expected_descriptor.name)),
      testing::Field(&PartitionDescriptor::type,
                     testing::ElementsAreArray(expected_descriptor.type)));
}

auto PartitionDescriptorMatchesEntry(const FvmSparsePartitionEntry& expected_descriptor) {
  return PartitionDescriptorEq(expected_descriptor.descriptor);
}

[[maybe_unused]] auto ExtentDescriptorEq(const fvm::ExtentDescriptor& expected_descriptor) {
  using ExtentDescriptor = fvm::ExtentDescriptor;
  return testing::AllOf(
      testing::Field(&ExtentDescriptor::magic, testing::Eq(expected_descriptor.magic)),
      testing::Field(&ExtentDescriptor::slice_start, testing::Eq(expected_descriptor.slice_start)),
      testing::Field(&ExtentDescriptor::slice_count, testing::Eq(expected_descriptor.slice_count)),
      testing::Field(&ExtentDescriptor::extent_length,
                     testing::Eq(expected_descriptor.extent_length)));
}

MATCHER(ExtentDescriptorsAreEq, "Compares to Extent Descriptors") {
  auto [a, b] = arg;
  return testing::ExplainMatchResult(ExtentDescriptorEq(b), a, result_listener);
};

auto ExtentDescriptorsMatchesEntry(const FvmSparsePartitionEntry& expected_entry) {
  return testing::Pointwise(ExtentDescriptorsAreEq(), expected_entry.extents);
}

TEST(FvmSparseImageTest, FvmSparseWriteImageDataUncompressedCompliesWithFormat) {
  SerializedImageContainer container;
  auto descriptor = MakeDescriptorWithOptions(MakeOptions(8192, CompressionSchema::kNone));
  auto header = FvmSparseGenerateHeader(descriptor);

  std::vector<FvmSparsePartitionEntry> expected_partition_entries =
      GetExpectedPartitionEntries(descriptor, descriptor.options().slice_size);

  auto write_result = FvmSparseWriteImage(descriptor, &container.writer());
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();
  ASSERT_EQ(write_result.value(), FvmSparseCalculateUncompressedImageSize(descriptor));

  EXPECT_THAT(container.serialized_image().header, HeaderEq(header));

  // Check partition and entry descriptors.
  auto it = descriptor.partitions().begin();
  const auto& partition_1 = *it++;
  auto partition_1_entry_result =
      FvmSparseGeneratePartitionEntry(descriptor.options().slice_size, partition_1);
  ASSERT_TRUE(partition_1_entry_result.is_ok()) << partition_1_entry_result.error();
  FvmSparsePartitionEntry partition_1_entry = partition_1_entry_result.take_value();
  EXPECT_THAT(container.serialized_image().partition_1.descriptor,
              PartitionDescriptorMatchesEntry(partition_1_entry));
  EXPECT_THAT(container.serialized_image().partition_1.extents,
              ExtentDescriptorsMatchesEntry(partition_1_entry));

  const auto& partition_2 = *it++;
  auto partition_2_entry_result =
      FvmSparseGeneratePartitionEntry(descriptor.options().slice_size, partition_2);
  ASSERT_TRUE(partition_2_entry_result.is_ok()) << partition_2_entry_result.error();
  FvmSparsePartitionEntry partition_2_entry = partition_2_entry_result.take_value();
  EXPECT_THAT(container.serialized_image().partition_2.descriptor,
              PartitionDescriptorMatchesEntry(partition_2_entry));
  EXPECT_THAT(container.serialized_image().partition_2.extents,
              ExtentDescriptorsMatchesEntry(partition_2_entry));

  // Check data is correct.
  uint64_t partition_index = 0;
  for (const auto& partition : descriptor.partitions()) {
    auto read_content = partition_index == 0 ? GetContents<1> : GetContents<2>;
    std::vector<uint8_t> expected_content;
    uint64_t extent_index = 0;
    auto extents = container.PartitionExtents(partition_index);
    for (const auto& mapping : partition.address().mappings) {
      expected_content.resize(mapping.count, 0);
      ASSERT_TRUE(read_content(mapping.source, expected_content).is_ok());
      EXPECT_THAT(extents[extent_index], testing::ElementsAreArray(expected_content));
      extent_index++;
    }
    partition_index++;
  }
}

TEST(FvmSparseImageTest, FvmSparseWriteImageDataCompressedCompliesWithFormat) {
  SerializedImageContainer container;
  auto descriptor = MakeDescriptorWithOptions(MakeOptions(8192, CompressionSchema::kLz4));
  auto header = FvmSparseGenerateHeader(descriptor);

  std::vector<FvmSparsePartitionEntry> expected_partition_entries =
      GetExpectedPartitionEntries(descriptor, descriptor.options().slice_size);

  Lz4Compressor compressor = Lz4Compressor::Create(descriptor.options().compression).take_value();
  auto write_result = FvmSparseWriteImage(descriptor, &container.writer(), &compressor);
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();
  ASSERT_LE(write_result.value(), FvmSparseCalculateUncompressedImageSize(descriptor));

  EXPECT_THAT(container.serialized_image().header, HeaderEq(header));
  uint64_t compressed_extents_size = write_result.value() - header.header_length;

  // Check partition and entry descriptors.
  auto it = descriptor.partitions().begin();
  const auto& partition_1 = *it++;
  auto partition_1_entry_result =
      FvmSparseGeneratePartitionEntry(descriptor.options().slice_size, partition_1);
  ASSERT_TRUE(partition_1_entry_result.is_ok()) << partition_1_entry_result.error();
  FvmSparsePartitionEntry partition_1_entry = partition_1_entry_result.take_value();
  EXPECT_THAT(container.serialized_image().partition_1.descriptor,
              PartitionDescriptorMatchesEntry(partition_1_entry));
  EXPECT_THAT(container.serialized_image().partition_1.extents,
              ExtentDescriptorsMatchesEntry(partition_1_entry));

  const auto& partition_2 = *it++;
  auto partition_2_entry_result =
      FvmSparseGeneratePartitionEntry(descriptor.options().slice_size, partition_2);
  ASSERT_TRUE(partition_2_entry_result.is_ok()) << partition_2_entry_result.error();
  FvmSparsePartitionEntry partition_2_entry = partition_2_entry_result.take_value();
  EXPECT_THAT(container.serialized_image().partition_2.descriptor,
              PartitionDescriptorMatchesEntry(partition_2_entry));
  EXPECT_THAT(container.serialized_image().partition_2.extents,
              ExtentDescriptorsMatchesEntry(partition_2_entry));

  // Decompress extent data.
  LZ4F_decompressionContext_t decompression_context = nullptr;
  auto release_decompressor = fbl::MakeAutoCall([&decompression_context]() {
    if (decompression_context != nullptr) {
      LZ4F_freeDecompressionContext(decompression_context);
    }
  });
  auto create_return_code = LZ4F_createDecompressionContext(&decompression_context, LZ4F_VERSION);
  ASSERT_FALSE(LZ4F_isError(create_return_code)) << LZ4F_getErrorName(create_return_code);

  std::vector<uint8_t> decompressed_extents(
      sizeof(SerializedSparseImage) - offsetof(SerializedSparseImage, extent_data), 0);
  size_t decompressed_byte_count = decompressed_extents.size();
  size_t consumed_compressed_bytes = compressed_extents_size;
  auto decompress_return_code = LZ4F_decompress(
      decompression_context, decompressed_extents.data(), &decompressed_byte_count,
      container.serialized_image().extent_data, &consumed_compressed_bytes, nullptr);
  ASSERT_FALSE(LZ4F_isError(decompress_return_code));
  ASSERT_EQ(decompressed_byte_count, decompressed_extents.size());
  ASSERT_EQ(consumed_compressed_bytes, compressed_extents_size);

  // Copy the uncompressed data over the compressed data.
  memcpy(container.serialized_image().extent_data, decompressed_extents.data(),
         decompressed_extents.size());
  uint64_t partition_index = 0;
  for (const auto& partition : descriptor.partitions()) {
    auto read_content = partition_index == 0 ? GetContents<1> : GetContents<2>;
    std::vector<uint8_t> expected_content;
    uint64_t extent_index = 0;
    auto extents = container.PartitionExtents(partition_index);
    for (const auto& mapping : partition.address().mappings) {
      expected_content.resize(mapping.count, 0);
      ASSERT_TRUE(read_content(mapping.source, expected_content).is_ok());
      EXPECT_THAT(extents[extent_index], testing::ElementsAreArray(expected_content));
      extent_index++;
    }
    partition_index++;
  }
}

class ErrorWriter final : public Writer {
 public:
  ErrorWriter(uint64_t error_offset, std::string_view error)
      : error_(error), error_offset_(error_offset) {}
  ~ErrorWriter() final = default;

  fit::result<void, std::string> Write([[maybe_unused]] uint64_t offset,
                                       [[maybe_unused]] fbl::Span<const uint8_t> buffer) final {
    if (offset >= error_offset_) {
      return fit::error(error_);
    }
    return fit::ok();
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
            return fit::error(std::string(kReadError));
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

TEST(FvmSparseImageTest, SparseReaderIsAbleToParseUncompressedSerializedData) {
  SerializedImageContainer container;
  auto descriptor = MakeDescriptorWithOptions(MakeOptions(8192, CompressionSchema::kNone));

  std::vector<FvmSparsePartitionEntry> expected_partition_entries =
      GetExpectedPartitionEntries(descriptor, descriptor.options().slice_size);

  auto write_result = FvmSparseWriteImage(descriptor, &container.writer());
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();

  std::unique_ptr<FvmSparseReaderImpl> sparse_reader_impl(
      new FvmSparseReaderImpl(container.AsSpan()));
  std::unique_ptr<fvm::SparseReader> sparse_reader = nullptr;
  // This verifies metadata(header, partition descriptors and extent descriptors.)
  ASSERT_EQ(ZX_OK, fvm::SparseReader::Create(std::move(sparse_reader_impl), &sparse_reader));
  ASSERT_THAT(sparse_reader->Image(), HeaderEq(container.serialized_image().header));

  // Partition 1 metadata.
  {
    const auto& partition_descriptor = sparse_reader->Partitions()[0];
    const auto partition_extent_descriptors = fbl::Span<const fvm::ExtentDescriptor>(
        reinterpret_cast<const fvm::ExtentDescriptor*>(
            reinterpret_cast<const uint8_t*>(&partition_descriptor + 1)),
        3);

    EXPECT_THAT(partition_descriptor,
                PartitionDescriptorEq(container.serialized_image().partition_1.descriptor));
    EXPECT_THAT(partition_extent_descriptors,
                testing::Pointwise(ExtentDescriptorsAreEq(),
                                   container.serialized_image().partition_1.extents));
  }

  // Partition 2 metadata.
  {
    off_t partition_2_offset = sizeof(fvm::PartitionDescriptor) + 3 * sizeof(fvm::ExtentDescriptor);
    const auto& partition_descriptor = *reinterpret_cast<fvm::PartitionDescriptor*>(
        reinterpret_cast<uint8_t*>(sparse_reader->Partitions()) + partition_2_offset);
    const auto partition_extent_descriptors = fbl::Span<const fvm::ExtentDescriptor>(
        reinterpret_cast<const fvm::ExtentDescriptor*>(
            reinterpret_cast<const uint8_t*>(&partition_descriptor + 1)),
        2);
    EXPECT_THAT(partition_descriptor,
                PartitionDescriptorEq(container.serialized_image().partition_2.descriptor));
    EXPECT_THAT(partition_extent_descriptors,
                testing::Pointwise(ExtentDescriptorsAreEq(),
                                   container.serialized_image().partition_2.extents));
  }

  uint64_t partition_index = 0;
  for (const auto& partition : descriptor.partitions()) {
    std::vector<uint8_t> read_content;
    uint64_t extent_index = 0;
    auto extents = container.PartitionExtents(partition_index);
    for (const auto& mapping : partition.address().mappings) {
      read_content.resize(mapping.count, 0);
      size_t read_bytes = 0;
      ASSERT_EQ(sparse_reader->ReadData(read_content.data(), read_content.size(), &read_bytes),
                ZX_OK);
      EXPECT_THAT(read_content, testing::ElementsAreArray(extents[extent_index]));
      extent_index++;
    }
    partition_index++;
  }
}

TEST(FvmSparseImageTest, SparseReaderIsAbleToParseCompressedSerializedData) {
  SerializedImageContainer container;
  auto descriptor = MakeDescriptorWithOptions(MakeOptions(8192, CompressionSchema::kLz4));

  std::vector<FvmSparsePartitionEntry> expected_partition_entries =
      GetExpectedPartitionEntries(descriptor, descriptor.options().slice_size);

  Lz4Compressor compressor = Lz4Compressor::Create(descriptor.options().compression).take_value();
  auto write_result = FvmSparseWriteImage(descriptor, &container.writer(), &compressor);
  ASSERT_TRUE(write_result.is_ok()) << write_result.error();

  std::unique_ptr<FvmSparseReaderImpl> sparse_reader_impl(
      new FvmSparseReaderImpl(container.AsSpan()));
  std::unique_ptr<fvm::SparseReader> sparse_reader = nullptr;
  // This verifies metadata(header, partition descriptors and extent descriptors.)
  ASSERT_EQ(ZX_OK, fvm::SparseReader::Create(std::move(sparse_reader_impl), &sparse_reader));
  ASSERT_THAT(sparse_reader->Image(), HeaderEq(container.serialized_image().header));

  // Partition 1 metadata.
  {
    const auto& partition_descriptor = sparse_reader->Partitions()[0];
    const auto partition_extent_descriptors = fbl::Span<const fvm::ExtentDescriptor>(
        reinterpret_cast<const fvm::ExtentDescriptor*>(
            reinterpret_cast<const uint8_t*>(&partition_descriptor + 1)),
        3);

    EXPECT_THAT(partition_descriptor,
                PartitionDescriptorEq(container.serialized_image().partition_1.descriptor));
    EXPECT_THAT(partition_extent_descriptors,
                testing::Pointwise(ExtentDescriptorsAreEq(),
                                   container.serialized_image().partition_1.extents));
  }

  // Partition 2 metadata.
  {
    off_t partition_2_offset = sizeof(fvm::PartitionDescriptor) + 3 * sizeof(fvm::ExtentDescriptor);
    const auto& partition_descriptor = *reinterpret_cast<fvm::PartitionDescriptor*>(
        reinterpret_cast<uint8_t*>(sparse_reader->Partitions()) + partition_2_offset);
    const auto partition_extent_descriptors = fbl::Span<const fvm::ExtentDescriptor>(
        reinterpret_cast<const fvm::ExtentDescriptor*>(
            reinterpret_cast<const uint8_t*>(&partition_descriptor + 1)),
        2);
    EXPECT_THAT(partition_descriptor,
                PartitionDescriptorEq(container.serialized_image().partition_2.descriptor));
    EXPECT_THAT(partition_extent_descriptors,
                testing::Pointwise(ExtentDescriptorsAreEq(),
                                   container.serialized_image().partition_2.extents));
  }

  // Check extent data.
  std::vector<uint8_t> read_content;
  std::vector<uint8_t> original_content;
  for (const auto& partition : descriptor.partitions()) {
    for (const auto& mapping : partition.address().mappings) {
      read_content.resize(mapping.count, 0);
      original_content.resize(mapping.count, 0);
      size_t read_bytes = 0;
      ASSERT_EQ(sparse_reader->ReadData(read_content.data(), read_content.size(), &read_bytes),
                ZX_OK);
      ASSERT_EQ(read_content.size(), read_bytes);
      auto read_result = partition.reader()->Read(
          mapping.source, fbl::Span(original_content.data(), original_content.size()));
      ASSERT_TRUE(read_result.is_ok()) << read_result.error();

      EXPECT_THAT(read_content, testing::ElementsAreArray(original_content));
    }
  }
}

}  // namespace
}  // namespace storage::volume_image
