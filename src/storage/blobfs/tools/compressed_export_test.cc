// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <ostream>
#include <vector>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/lib/chunked-compression/chunked-decompressor.h"
#include "src/lib/chunked-compression/status.h"
#include "src/lib/json_parser/json_parser.h"
#include "src/storage/blobfs/tools/blobfs_creator.h"

namespace {

void CleanDir(const std::filesystem::path& temp_path) {
  if (std::filesystem::exists(temp_path)) {
    ASSERT_TRUE(std::filesystem::remove_all(temp_path))
        << "Failed to delete old directory: " << temp_path;
  }
}

void CleanAndCreateDir(const std::filesystem::path& temp_path) {
  ASSERT_NO_FATAL_FAILURE(CleanDir(temp_path));

  ASSERT_TRUE(std::filesystem::create_directories(temp_path))
      << "Failed to create temp directory: " << temp_path;
}

class CompressedExportTest : public testing::Test {
 public:
  void SetUp() override {
    temp_path_ = std::filesystem::temp_directory_path().append(std::tmpnam(nullptr));
    ASSERT_NO_FATAL_FAILURE(CleanAndCreateDir(temp_path_));
  }

  // Intentionally does not assert on failed cleanup.
  void TearDown() override { CleanDir(temp_path_); }

  const std::filesystem::path& temp_path() { return temp_path_; }

 private:
  std::filesystem::path temp_path_;
};

TEST_F(CompressedExportTest, ExportAndVerifyZeroesFile) {
  const uint64_t kInputFileSize = 1ul << 20;

  // Generate compressible file.
  auto in_file_path = std::filesystem::path(temp_path()).append("input.blob");
  {
    fbl::unique_fd in_file(open(in_file_path.c_str(), O_RDWR | O_CREAT, 0644));
    ASSERT_TRUE(in_file.is_valid()) << "Failed to open file for writing: " << in_file_path;

    // 1 MB of empty file is highly compressible.
    ASSERT_EQ(ftruncate(in_file.get(), kInputFileSize), 0)
        << "ftruncate on " << in_file_path << " failed: " << strerror(errno);
  }

  // Run host tool.
  auto json_path = std::filesystem::path(temp_path()).append("manifest.json");
  {
    auto blobfs_image_path = std::filesystem::path(temp_path()).append("blobfs.blk");
    auto exported_prefix = std::filesystem::path(temp_path()).append("exported-");
    std::vector<std::string> string_args = {"blobfs",
                                            "--json-output",
                                            json_path,
                                            "--compress",
                                            blobfs_image_path,
                                            "mkfs",
                                            "--compressed_copy_prefix",
                                            exported_prefix,
                                            "--blob",
                                            in_file_path};

    // Stuffing the argument list into a mutable char** so that getopts can consume it.
    std::vector<char*> args;
    args.reserve(string_args.size());
    for (auto& str : string_args) {
      args.push_back(str.data());
    }

    BlobfsCreator creator;
    ASSERT_EQ(creator.ProcessAndRun(static_cast<int>(args.size()), args.data()), ZX_OK);
  }

  // Load up json file to get compressed entry.
  fbl::unique_fd output_file;
  size_t output_size;
  {
    json_parser::JSONParser parser;
    rapidjson::Document document = parser.ParseFromFile(json_path.c_str());
    ASSERT_FALSE(parser.HasError())
        << "Failed to parse json file " << json_path << ": " << parser.error_str();
    ASSERT_TRUE(document.IsArray()) << "Top level item should be an array";
    ASSERT_EQ(document.Size(), 1u) << "Expected only 1 blob in the json array.";
    rapidjson::GenericObject entry = document[0].GetObject();
    auto itr = entry.FindMember("source_path");
    ASSERT_NE(itr, entry.MemberEnd()) << "Failed to find source_entry in json";
    ASSERT_TRUE(itr->value.IsString());
    ASSERT_EQ(std::filesystem::canonical(itr->value.GetString()),
              std::filesystem::canonical(in_file_path));
    auto out_itr = entry.FindMember("compressed_source_path");
    ASSERT_NE(out_itr, entry.MemberEnd()) << "Failed to find entry for compressed_source_path.";
    ASSERT_TRUE(out_itr->value.IsString());
    output_file.reset(open(out_itr->value.GetString(), O_RDWR));
    ASSERT_TRUE(output_file.is_valid())
        << "Failed to open compressed file: " << out_itr->value.GetString();
    // compressed_file_size is an upperbound, and may be smaller.
    auto size_itr = entry.FindMember("compressed_file_size");
    ASSERT_NE(size_itr, entry.MemberEnd()) << "Failed to find entry for compressed_file_size.";
    ASSERT_TRUE(size_itr->value.IsUint64()) << "Entry compressed_file_size should be uint64";
    output_size = size_itr->value.GetUint64();
  }

  // Read in compressed version.
  std::vector<uint8_t> compressed_bytes(output_size);
  {
    size_t offset = 0;
    while (offset < output_size) {
      size_t len = read(output_file.get(), &compressed_bytes[offset], output_size - offset);
      if (len <= 0) {
        break;
      }
      offset += len;
    }
    ASSERT_LE(offset, output_size)
        << "Reading back compressed file was too large. Json indicated a max size of "
        << output_size;
  }

  // Verify compressed version.
  {
    chunked_compression::SeekTable seek_table;
    chunked_compression::HeaderReader reader;
    ASSERT_EQ(reader.Parse(compressed_bytes.data(), kInputFileSize, kInputFileSize, &seek_table),
              ZX_OK);

    std::vector<uint8_t> decompressed_bytes(kInputFileSize);
    chunked_compression::ChunkedDecompressor decompressor;
    size_t actual;
    ASSERT_EQ(
        decompressor.Decompress(seek_table, compressed_bytes.data(), seek_table.CompressedSize(),
                                decompressed_bytes.data(), kInputFileSize, &actual),
        chunked_compression::kStatusOk);
    ASSERT_EQ(actual, kInputFileSize) << "Decompressed file was expected to be " << kInputFileSize
                                      << " but was actually " << actual;
    fbl::unique_fd original_file(open(in_file_path.c_str(), O_RDONLY));
    ASSERT_TRUE(original_file.is_valid());
    std::vector<uint8_t> verity(1 << 7);
    size_t offset = 0;
    while (offset < kInputFileSize) {
      size_t readback = read(original_file.get(), verity.data(), verity.size());
      ASSERT_GT(readback, 0ul) << "Failed to read expected data from original file.";
      ASSERT_EQ(memcmp(verity.data(), &decompressed_bytes[offset], readback), 0)
          << "Decompressed data did not match original between " << offset << " and "
          << offset + readback;
      offset += readback;
    }
  }
}

}  // namespace
