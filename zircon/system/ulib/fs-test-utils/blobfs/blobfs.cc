// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fs-test-utils/blobfs/blobfs.h>
#include <lib/fdio/io.h>
#include <zircon/syscalls.h>

namespace fs_test_utils {

using digest::MerkleTree;
using digest::Digest;
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
bool GenerateBlob(BlobSrcFunction sourceCb, fbl::String mount_path,
                  size_t size_data, std::unique_ptr<BlobInfo>* out) {
    std::unique_ptr<BlobInfo> info(new BlobInfo);
    info->data.reset(new char[size_data]);
    sourceCb(info->data.get(), size_data);
    info->size_data = size_data;

    // Generate the Merkle Tree
    info->size_merkle = MerkleTree::GetTreeLength(size_data);
    if (info->size_merkle == 0) {
        info->merkle = nullptr;
    } else {
        info->merkle.reset(new char[info->size_merkle]);
    }
    Digest digest;
    zx_status_t status = MerkleTree::Create(&info->data[0], info->size_data, &info->merkle[0],
                                            info->size_merkle, &digest);
    if (status != ZX_OK) {
        printf("Couldn't create Merkle Tree\n");
        return false;
    }
    strcpy(info->path, mount_path.c_str());
    size_t mount_path_len = strlen(info->path);
    strcpy(info->path + mount_path_len, "/");
    size_t prefix_len = strlen(info->path);
    digest.ToString(info->path + prefix_len, sizeof(info->path) - prefix_len);

    // Sanity-check the merkle tree
    status = MerkleTree::Verify(&info->data[0], info->size_data, &info->merkle[0],
                                 info->size_merkle, 0, info->size_data, digest);
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

} // namespace fs_test_utils
