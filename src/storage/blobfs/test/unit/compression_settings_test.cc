// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/compression_settings.h"

#include <gtest/gtest.h>
#include <src/lib/chunked-compression/compression-params.h>

#include "src/storage/blobfs/format.h"

namespace blobfs {
namespace {

// Simple basic conversion test.
TEST(CompressionSettingsTest, CompressionAlgorithmToStringConvertChunked) {
  ASSERT_STREQ(CompressionAlgorithmToString(CompressionAlgorithm::kChunked), "ZSTD_CHUNKED");
}

// Simple basic conversion for compression enabled.
TEST(CompressionSettingsTest, AlgorithmForInodeConvertChunked) {
  Inode inode;
  inode.header.flags = kBlobFlagChunkCompressed;
  ASSERT_EQ(AlgorithmForInode(inode), CompressionAlgorithm::kChunked);
}

// Conversion when no compression flags are enabled.
TEST(CompressionSettingsTest, AlgorithmForInodeConvertUncompressed) {
  Inode inode;
  inode.header.flags &= ~kBlobFlagMaskAnyCompression;
  ASSERT_EQ(AlgorithmForInode(inode).value(), CompressionAlgorithm::kUncompressed);
}

// Simple basic conversion test.
TEST(CompressionSettingsTest, CompressionInodeHeaderFlagsConvertChunked) {
  ASSERT_EQ(CompressionInodeHeaderFlags(CompressionAlgorithm::kChunked), kBlobFlagChunkCompressed);
}

// Apply a couple of CompressionAlgorithms, verify that they come back right despite multiple calls.
TEST(CompressionSettingsTest, SetCompressionAlgorithmCalledTwice) {
  Inode inode;
  inode.header.flags = kBlobFlagAllocated;  // Ensure that this stays set.
  SetCompressionAlgorithm(&inode, CompressionAlgorithm::kChunked);
  ASSERT_EQ(inode.header.flags, kBlobFlagChunkCompressed | kBlobFlagAllocated);
  SetCompressionAlgorithm(&inode, CompressionAlgorithm::kChunked);
  ASSERT_EQ(inode.header.flags, kBlobFlagChunkCompressed | kBlobFlagAllocated);
}

// Anything is valid with no compression level setings.
TEST(CompressionSettingsTest, IsValidWithNoSettings) {
  CompressionSettings settings = {CompressionAlgorithm::kUncompressed, std::nullopt};
  ASSERT_TRUE(settings.IsValid());
}

// There should be no compression settings for UNCOMPRESSED.
TEST(CompressionSettingsTest, IsValidCompressionLevelUncompressed) {
  CompressionSettings settings = {CompressionAlgorithm::kUncompressed, 4};
  ASSERT_FALSE(settings.IsValid());
}

// Check range limits on Chunked compression.
TEST(CompressionSettingsTest, IsValidCompressionLevelChunked) {
  CompressionSettings settings = {CompressionAlgorithm::kChunked,
                                  chunked_compression::CompressionParams::MinCompressionLevel()};
  ASSERT_TRUE(settings.IsValid());
  settings.compression_level = chunked_compression::CompressionParams::MaxCompressionLevel() + 1;
  ASSERT_FALSE(settings.IsValid());
}

}  // namespace
}  // namespace blobfs
