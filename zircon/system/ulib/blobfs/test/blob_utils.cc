// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/blob_utils.h"

#include <fcntl.h>
#include <lib/fdio/io.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <algorithm>

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

namespace blobfs {
namespace {

using digest::Digest;
using digest::MerkleTreeCreator;
using digest::MerkleTreeVerifier;

fbl::Array<uint8_t> LoadTemplateData() {
  // TODO(jfsulliv): Load aux file properly. Requires componentizing the blobfs tests which use
  // this helper.
  // XXX DO NOT COPY THIS. This is not a well supported mechanism for loading test data.
  const char* file = "bin/test-binary";
  const char* root_dir = getenv("TEST_ROOT_DIR");
  EXPECT_NE("", root_dir);
  if (!root_dir)
    return {};
  std::string path = std::string(root_dir) + "/" + file;

  fbl::unique_fd fd(open(path.c_str(), O_RDONLY));
  EXPECT_TRUE(fd.is_valid());
  if (!fd)
    return {};
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
                  std::unique_ptr<BlobInfo>* out) {
  std::unique_ptr<BlobInfo> info(new BlobInfo);
  info->data.reset(new char[data_size]);
  data_generator(info->data.get(), data_size);
  info->size_data = data_size;

  Digest digest;
  std::unique_ptr<uint8_t[]> tree;
  ASSERT_OK(MerkleTreeCreator::Create(info->data.get(), info->size_data, &tree, &info->size_merkle,
                                      &digest));
  info->merkle.reset(reinterpret_cast<char*>(tree.release()));
  snprintf(info->path, sizeof(info->path), "%s/%s", mount_path.c_str(), digest.ToString().c_str());

  // Sanity-check the merkle tree.
  ASSERT_OK(MerkleTreeVerifier::Verify(info->data.get(), info->size_data, 0, info->size_data,
                                       info->merkle.get(), info->size_merkle, digest));
  *out = std::move(info);
}

void GenerateRandomBlob(const std::string& mount_path, size_t data_size,
                        std::unique_ptr<BlobInfo>* out) {
  GenerateBlob(RandomFill, mount_path, data_size, out);
}

void GenerateRealisticBlob(const std::string& mount_path, size_t data_size,
                           std::unique_ptr<BlobInfo>* out) {
  static fbl::Array<uint8_t> template_data = LoadTemplateData();
  ASSERT_GT(template_data.size(), 0);
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
      mount_path, data_size, out);
}

void VerifyContents(int fd, const char* data, size_t data_size) {
  ASSERT_EQ(0, lseek(fd, 0, SEEK_SET));

  constexpr size_t kBuffersize = 8192;
  std::unique_ptr<char[]> buffer(new char[kBuffersize]);

  for (size_t total_read = 0; total_read < data_size; total_read += kBuffersize) {
    size_t read_size = std::min(kBuffersize, data_size - total_read);
    ASSERT_EQ(read_size, read(fd, buffer.get(), read_size));
    ASSERT_BYTES_EQ(&data[total_read], buffer.get(), read_size);
  }
}

}  // namespace blobfs
