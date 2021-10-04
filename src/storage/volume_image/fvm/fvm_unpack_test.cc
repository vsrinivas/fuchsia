// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/fvm/fvm_unpack.h"

#include <iterator>

#include <gtest/gtest.h>

#include "lib/fpromise/result.h"
#include "lib/stdcompat/span.h"
#include "src/storage/fvm/format.h"
#include "src/storage/fvm/metadata.h"
#include "src/storage/volume_image/fvm/fvm_descriptor.h"
#include "src/storage/volume_image/fvm/options.h"
#include "src/storage/volume_image/utils/reader.h"
#include "src/storage/volume_image/utils/writer.h"

namespace storage::volume_image {
namespace {

// A simple in-memory reader/writer to do both.
class RamReaderWriter : public Reader, public Writer {
 public:
  static constexpr uint64_t kPageSize = 4096;
  RamReaderWriter() = default;
  explicit RamReaderWriter(uint64_t length) : length_(length) {}
  RamReaderWriter(const RamReaderWriter&) = delete;
  RamReaderWriter& operator=(const RamReaderWriter&) = delete;

  // Reader interface
  uint64_t length() const final { return length_; }
  fpromise::result<void, std::string> Read(uint64_t offset,
                                           cpp20::span<uint8_t> buffer) const final {
    if (offset + buffer.size() > length_) {
      return fpromise::error("Read exceeds size of buffer");
    }
    uint64_t progress = 0;
    uint64_t page_number = offset / kPageSize;
    while (buffer.size() - progress > 0) {
      uint64_t to_read =
          std::min(kPageSize - ((offset + progress) % kPageSize), buffer.size() - progress);
      auto page = data_.find(page_number);
      if (page == data_.end()) {
        memset(&buffer[progress], 0, to_read);
      } else {
        memcpy(&buffer[progress], &page->second[(offset + progress) % kPageSize], to_read);
      }
      progress += to_read;
      page_number++;
    }
    return fpromise::ok();
  }

  // Writer interface
  fpromise::result<void, std::string> Write(uint64_t offset,
                                            cpp20::span<const uint8_t> buffer) final {
    uint64_t progress = 0;
    uint64_t page_number = offset / kPageSize;
    while (buffer.size() - progress > 0) {
      uint64_t to_write =
          std::min(kPageSize - ((offset + progress) % kPageSize), buffer.size() - progress);
      data_.try_emplace(page_number);
      memcpy(&data_[page_number][(offset + progress) % kPageSize], &buffer[progress], to_write);
      progress += to_write;
      page_number++;
    }
    if (offset + buffer.size() > length_) {
      length_ = offset + buffer.size();
    }
    return fpromise::ok();
  }

 private:
  std::unordered_map<uint64_t, std::array<uint8_t, kPageSize>> data_;
  uint64_t length_ = 0;
};

FvmOptions MakeOptions() {
  static const FvmOptions kOptions = {
      .target_volume_size = 20u * (1u << 20),
      .slice_size = static_cast<uint64_t>(32) * (1u << 10),
  };

  return kOptions;
}

fvm::VPartitionEntry MakePartitionEntry(const std::string& name, uint64_t slice_count) {
  return fvm::VPartitionEntry(fvm::kPlaceHolderInstanceGuid.data(),
                              fvm::kPlaceHolderInstanceGuid.data(), slice_count, name);
}

// Set the first byte and last byte of a slice to some value.
void SetSlice(const fvm::Metadata& metadata, uint64_t pslice, Writer* writer, uint8_t value) {
  writer->Write(metadata.GetHeader().GetSliceDataOffset(pslice), cpp20::span(&value, 1));
  writer->Write(metadata.GetHeader().GetSliceDataOffset(pslice + 1) - 1, cpp20::span(&value, 1));
}

// Get the first and last bytes of a slice from an outputted block file. Ensure they are equal.
uint8_t GetBlockSlice(uint64_t slice_size, uint64_t pslice, Reader* reader) {
  uint8_t value = 0;
  EXPECT_TRUE(reader->Read(pslice * slice_size, cpp20::span(&value, 1)).is_ok());
  uint8_t value2 = 0;
  EXPECT_TRUE(reader->Read((pslice + 1) * slice_size - 1, cpp20::span(&value2, 1)).is_ok());
  EXPECT_EQ(value, value2);
  return value;
}

TEST(FvmUnpackTest, BasicSuccess) {
  auto options = MakeOptions();
  const auto header = internal::MakeHeader(options, 100);
  std::vector<fvm::VPartitionEntry> partitions = {MakePartitionEntry("part1", 2),
                                                  MakePartitionEntry("part2", 3)};
  // Mixed around the ordering a bit.
  std::array<fvm::SliceEntry, 5> slices;
  slices[0].Set(2, 1);
  slices[1].Set(1, 0);
  slices[2].Set(2, 0);
  slices[3].Set(1, 1);
  slices[4].Set(2, 2);

  auto metadata_or = fvm::Metadata::Synthesize(header, partitions.data(), partitions.size(),
                                               slices.data(), slices.size());
  ASSERT_TRUE(metadata_or.is_ok());
  fvm::Metadata metadata(std::move(metadata_or.value()));
  RamReaderWriter block;
  block.Write(0, cpp20::span<const uint8_t>(static_cast<uint8_t*>(metadata.Get()->data()),
                                            metadata.Get()->size()));
  for (uint64_t i = 0; i < slices.size(); ++i) {
    // These are 1 indexed inside of FVM.
    SetSlice(metadata, i + 1, &block, i);
  }

  std::vector<std::unique_ptr<Writer>> parts(3);
  parts[1] = std::make_unique<RamReaderWriter>();
  parts[2] = std::make_unique<RamReaderWriter>();

  ASSERT_TRUE(internal::UnpackRawFvmPartitions(block, metadata, parts).is_ok());

  for (uint64_t i = 0; i < slices.size(); ++i) {
    ASSERT_EQ(GetBlockSlice(options.slice_size, slices[i].VSlice(),
                            static_cast<RamReaderWriter*>(parts[slices[i].VPartition()].get())),
              i);
  }
}

TEST(FvmUnpackTest, SkipUnlistedPartition) {
  auto options = MakeOptions();
  const auto header = internal::MakeHeader(options, 100);
  std::vector<fvm::VPartitionEntry> partitions = {MakePartitionEntry("part1", 2),
                                                  MakePartitionEntry("part2", 3)};
  // Mixed around the ordering a bit.
  std::array<fvm::SliceEntry, 5> slices;
  slices[0].Set(2, 1);
  slices[1].Set(1, 0);
  slices[2].Set(2, 0);
  slices[3].Set(1, 1);
  slices[4].Set(2, 2);

  auto metadata_or = fvm::Metadata::Synthesize(header, partitions.data(), partitions.size(),
                                               slices.data(), slices.size());
  ASSERT_TRUE(metadata_or.is_ok());
  fvm::Metadata metadata(std::move(metadata_or.value()));
  RamReaderWriter block;
  block.Write(0, cpp20::span<const uint8_t>(static_cast<uint8_t*>(metadata.Get()->data()),
                                            metadata.Get()->size()));
  for (uint64_t i = 0; i < slices.size(); ++i) {
    // These are 1 indexed inside of FVM.
    SetSlice(metadata, i + 1, &block, i);
  }

  std::vector<std::unique_ptr<Writer>> parts(2);
  parts[1] = std::make_unique<RamReaderWriter>();

  ASSERT_TRUE(internal::UnpackRawFvmPartitions(block, metadata, parts).is_ok());

  for (uint64_t i = 0; i < slices.size(); ++i) {
    // Only listed partition 1, should work while nothing else is done.
    if (slices[i].VPartition() != 1) {
      continue;
    }
    ASSERT_EQ(GetBlockSlice(options.slice_size, slices[i].VSlice(),
                            static_cast<RamReaderWriter*>(parts[slices[i].VPartition()].get())),
              i);
  }
}

TEST(FvmUnpackTest, InvalidHeader) {
  RamReaderWriter block(2 *
                        (sizeof(fvm::Header) + fvm::kMaxVPartitions * sizeof(fvm::VPartitionEntry) +
                         fvm::kMaxVSlices * sizeof(fvm::SliceEntry)));

  ASSERT_TRUE(UnpackRawFvm(block, "Some-Prefix").is_error());
}

TEST(FvmUnpackTest, NameDisambiguation) {
  std::vector<std::optional<std::string>> before = {
      std::nullopt, "", "My-file", "other_file", "My-file", "", std::nullopt, "My_file",
  };
  std::vector<std::optional<std::string>> after = {
      std::nullopt,
      "-0",       // empty name always gets a suffix
      "My_file",  // dash to underscore
      "other_file",
      "My_file-1",  // dash to underscore duplicate
      "-1",         // empty name always gets a suffix
      std::nullopt,
      "My_file-2",  // Duplicate already had an underscore
  };
  ASSERT_EQ(internal::DisambiguateNames(before), after);
}

}  //  namespace
}  // namespace storage::volume_image
