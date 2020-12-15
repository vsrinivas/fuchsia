// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/test/blob_utils.h"

#include <fcntl.h>
#include <lib/fdio/io.h>
#include <lib/zx/status.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <memory>

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/storage/blobfs/blob-layout.h"

namespace blobfs {
namespace {

using digest::Digest;
using digest::MerkleTreeCreator;
using digest::MerkleTreeVerifier;

fbl::Array<uint8_t> LoadTemplateData() {
  constexpr char kDataFile[] = "/pkg/data/test_binary";
  fbl::unique_fd fd(open(kDataFile, O_RDONLY));
  EXPECT_TRUE(fd.is_valid());
  if (!fd) {
    fprintf(stderr, "blob_utils.cc: Failed to load template data file %s: %s\n", kDataFile,
            strerror(errno));
    return {};
  }
  struct stat s;
  EXPECT_EQ(fstat(fd.get(), &s), 0);
  size_t sz = s.st_size;

  fbl::Array<uint8_t> data(new uint8_t[sz], sz);
  EXPECT_EQ(StreamAll(read, fd.get(), data.get(), sz), 0);
  return data;
}

}  // namespace

void RandomFill(char* data, size_t length) {
  for (size_t i = 0; i < length; i++) {
    // TODO(jfsulliv): Use explicit seed
    data[i] = (char)rand();
  }
}

// Creates, writes, reads (to verify) and operates on a blob.
void GenerateBlob(BlobSrcFunction data_generator, const std::string& mount_path, size_t data_size,
                  BlobLayoutFormat blob_layout_format, std::unique_ptr<BlobInfo>* out) {
  std::unique_ptr<BlobInfo> info(new BlobInfo);
  info->data.reset(new char[data_size]);
  data_generator(info->data.get(), data_size);
  info->size_data = data_size;

  auto merkle_tree = CreateMerkleTree(reinterpret_cast<const uint8_t*>(info->data.get()), data_size,
                                      ShouldUseCompactMerkleTreeFormat(blob_layout_format));
  ASSERT_EQ(merkle_tree.status_value(), ZX_OK);

  info->merkle.reset(reinterpret_cast<char*>(merkle_tree->merkle_tree.release()));
  info->size_merkle = merkle_tree->merkle_tree_size;
  snprintf(info->path, sizeof(info->path), "%s/%s", mount_path.c_str(),
           merkle_tree->root.ToString().c_str());

  // Sanity-check the merkle tree.
  MerkleTreeVerifier mtv;
  mtv.SetUseCompactFormat(ShouldUseCompactMerkleTreeFormat(blob_layout_format));
  ASSERT_EQ(mtv.SetDataLength(info->size_data), ZX_OK);
  ASSERT_EQ(mtv.SetTree(info->merkle.get(), info->size_merkle, merkle_tree->root.get(),
                        merkle_tree->root.len()),
            ZX_OK);
  ASSERT_EQ(mtv.Verify(info->data.get(), info->size_data, /*data_off=*/0), ZX_OK);

  *out = std::move(info);
}

void GenerateBlob(BlobSrcFunction data_generator, const std::string& mount_path, size_t data_size,
                  std::unique_ptr<BlobInfo>* out) {
  GenerateBlob(data_generator, mount_path, data_size, BlobLayoutFormat::kPaddedMerkleTreeAtStart,
               out);
}

void GenerateRandomBlob(const std::string& mount_path, size_t data_size,
                        BlobLayoutFormat blob_layout_format, std::unique_ptr<BlobInfo>* out) {
  GenerateBlob(RandomFill, mount_path, data_size, blob_layout_format, out);
}

void GenerateRandomBlob(const std::string& mount_path, size_t data_size,
                        std::unique_ptr<BlobInfo>* out) {
  GenerateRandomBlob(mount_path, data_size, BlobLayoutFormat::kPaddedMerkleTreeAtStart, out);
}

void GenerateRealisticBlob(const std::string& mount_path, size_t data_size,
                           BlobLayoutFormat blob_layout_format, std::unique_ptr<BlobInfo>* out) {
  static fbl::Array<uint8_t> template_data = LoadTemplateData();
  ASSERT_GT(template_data.size(), 0ul);
  GenerateBlob(
      [](char* data, size_t length) {
        // TODO(jfsulliv): Use explicit seed
        int nonce = rand();
        size_t nonce_size = std::min(sizeof(nonce), length);
        memcpy(data, &nonce, nonce_size);
        data += nonce_size;
        length -= nonce_size;

        while (length > 0) {
          size_t to_copy = std::min(template_data.size(), length);
          memcpy(data, template_data.get(), to_copy);
          data += to_copy;
          length -= to_copy;
        }
      },
      mount_path, data_size, blob_layout_format, out);
}

void VerifyContents(int fd, const char* data, size_t data_size) {
  ASSERT_EQ(0, lseek(fd, 0, SEEK_SET));

  constexpr size_t kBuffersize = 8192;
  std::unique_ptr<char[]> buffer(new char[kBuffersize]);

  for (size_t total_read = 0; total_read < data_size; total_read += kBuffersize) {
    ssize_t read_size = std::min(kBuffersize, data_size - total_read);
    ASSERT_EQ(read_size, read(fd, buffer.get(), read_size));
    ASSERT_EQ(memcmp(&data[total_read], buffer.get(), read_size), 0);
  }
}

void MakeBlob(const BlobInfo* info, fbl::unique_fd* fd) {
  fd->reset(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(*fd);
  ASSERT_EQ(ftruncate(fd->get(), info->size_data), 0);
  ASSERT_EQ(StreamAll(write, fd->get(), info->data.get(), info->size_data), 0);
  VerifyContents(fd->get(), info->data.get(), info->size_data);
}

std::string GetBlobLayoutFormatNameForTests(BlobLayoutFormat format) {
  switch (format) {
    case BlobLayoutFormat::kPaddedMerkleTreeAtStart:
      return "PaddedMerkleTreeAtStartLayout";
    case BlobLayoutFormat::kCompactMerkleTreeAtEnd:
      return "CompactMerkleTreeAtEndLayout";
  }
}

zx::status<MerkleTreeInfo> CreateMerkleTree(const uint8_t* data, int64_t data_size,
                                            bool use_compact_format) {
  MerkleTreeInfo merkle_tree_info;
  MerkleTreeCreator mtc;
  zx_status_t status;
  mtc.SetUseCompactFormat(use_compact_format);
  if ((status = mtc.SetDataLength(data_size)) != ZX_OK) {
    return zx::error(status);
  }
  merkle_tree_info.merkle_tree_size = mtc.GetTreeLength();
  if (merkle_tree_info.merkle_tree_size > 0) {
    merkle_tree_info.merkle_tree.reset(new uint8_t[merkle_tree_info.merkle_tree_size]);
  }
  uint8_t merkle_tree_root[digest::kSha256Length];
  if ((status = mtc.SetTree(merkle_tree_info.merkle_tree.get(), merkle_tree_info.merkle_tree_size,
                            merkle_tree_root, digest::kSha256Length)) != ZX_OK) {
    return zx::error(status);
  }
  if ((status = mtc.Append(data, data_size)) != ZX_OK) {
    return zx::error(status);
  }
  merkle_tree_info.root = merkle_tree_root;
  return zx::ok(std::move(merkle_tree_info));
}

}  // namespace blobfs
