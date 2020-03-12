// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/blob_utils.h"

#include <fcntl.h>
#include <lib/fdio/io.h>
#include <stdio.h>
#include <zircon/syscalls.h>

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

namespace blobfs {

using digest::Digest;
using digest::MerkleTreeCreator;
using digest::MerkleTreeVerifier;
using BlobSrcFunction = void (*)(char* data, size_t length);

void RandomFill(char* data, size_t length) {
  static unsigned int seed = static_cast<unsigned int>(zx_ticks_get());
  // TODO(US-286): Make this easier to reproduce with reliably generated prng.
  printf("RandomFill of %zu bytes with seed: %u\n", length, seed);
  for (size_t i = 0; i < length; i++) {
    data[i] = (char)rand_r(&seed);
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
  ASSERT_OK(MerkleTreeCreator::Create(info->data.get(), info->size_data, &tree,
                                      &info->size_merkle, &digest));
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
