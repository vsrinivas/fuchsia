// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fdio/io.h>
#include <stdio.h>
#include <zircon/syscalls.h>

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fs-test-utils/blobfs/blobfs.h>

namespace fs_test_utils {

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
// Returns the result of the post-processing 'func' (true == success).
bool GenerateBlob(BlobSrcFunction sourceCb, fbl::String mount_path, size_t size_data,
                  std::unique_ptr<BlobInfo>* out) {
  std::unique_ptr<BlobInfo> info(new BlobInfo);
  info->data.reset(new char[size_data]);
  sourceCb(info->data.get(), size_data);
  info->size_data = size_data;

  // Generate the Merkle Tree
  Digest digest;
  std::unique_ptr<uint8_t[]> tree;
  zx_status_t status = MerkleTreeCreator::Create(info->data.get(), info->size_data, &tree,
                                                 &info->size_merkle, &digest);
  info->merkle.reset(reinterpret_cast<char*>(tree.release()));
  if (status != ZX_OK) {
    printf("Couldn't create Merkle Tree\n");
    return false;
  }
  snprintf(info->path, sizeof(info->path), "%s/%s", mount_path.c_str(), digest.ToString().c_str());

  // Sanity-check the merkle tree
  status = MerkleTreeVerifier::Verify(info->data.get(), info->size_data, 0, info->size_data,
                                      info->merkle.get(), info->size_merkle, digest);
  if (status != ZX_OK) {
    printf("Failed to validate Merkle Tree\n");
    return false;
  }
  *out = std::move(info);
  return true;
}

bool GenerateRandomBlob(fbl::String mount_path, size_t size_data, std::unique_ptr<BlobInfo>* out) {
  return GenerateBlob(RandomFill, mount_path, size_data, out);
}

bool VerifyContents(int fd, const char* data, size_t size_data) {
  // Verify the contents of the Blob.
  constexpr size_t kReadSize = 8192;
  std::unique_ptr<char[]> buffer(new char[kReadSize]);
  if (lseek(fd, 0, SEEK_SET) != 0) {
    printf("Failed to seek to start\n");
    return false;
  }

  size_t total_read = 0;
  while (total_read != size_data) {
    ssize_t result = read(fd, buffer.get(), kReadSize);
    if (result <= 0) {
      printf("Failed to read file. Result: %li\n", result);
      return false;
    }
    if (memcmp(buffer.get(), &data[total_read], result) != 0) {
      printf("Blob contents differ\n");
      return false;
    }
    total_read += result;
  }
  return true;
}

}  // namespace fs_test_utils
