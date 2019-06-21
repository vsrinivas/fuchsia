// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/alloc_checker.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fs-test-utils/blobfs/blobfs.h>
#include <lib/fdio/io.h>
#include <unittest/unittest.h>
#include <zircon/syscalls.h>

namespace fs_test_utils {

using digest::MerkleTree;
using digest::Digest;
using BlobSrcFunction = void (*)(char* data, size_t length);

void RandomFill(char* data, size_t length) {
    static unsigned int seed = static_cast<unsigned int>(zx_ticks_get());
    // TODO(US-286): Make this easier to reproduce with reliably generated prng.
    unittest_printf("RandomFill of %zu bytes with seed: %u\n", length, seed);
    for (size_t i = 0; i < length; i++) {
        data[i] = (char)rand_r(&seed);
    }
}

// Creates, writes, reads (to verify) and operates on a blob.
// Returns the result of the post-processing 'func' (true == success).
bool GenerateBlob(BlobSrcFunction sourceCb, fbl::String mount_path,
                  size_t size_data, fbl::unique_ptr<BlobInfo>* out) {
    BEGIN_HELPER;
    fbl::AllocChecker ac;
    fbl::unique_ptr<BlobInfo> info(new (&ac) BlobInfo);
    EXPECT_EQ(ac.check(), true);
    info->data.reset(new (&ac) char[size_data]);
    EXPECT_EQ(ac.check(), true);
    sourceCb(info->data.get(), size_data);
    info->size_data = size_data;

    // Generate the Merkle Tree
    info->size_merkle = MerkleTree::GetTreeLength(size_data);
    if (info->size_merkle == 0) {
        info->merkle = nullptr;
    } else {
        info->merkle.reset(new (&ac) char[info->size_merkle]);
        ASSERT_EQ(ac.check(), true);
    }
    Digest digest;
    ASSERT_EQ(MerkleTree::Create(&info->data[0], info->size_data, &info->merkle[0],
                                 info->size_merkle, &digest),
              ZX_OK, "Couldn't create Merkle Tree");
    strcpy(info->path, mount_path.c_str());
    size_t mount_path_len = strlen(info->path);
    strcpy(info->path + mount_path_len, "/");
    size_t prefix_len = strlen(info->path);
    digest.ToString(info->path + prefix_len, sizeof(info->path) - prefix_len);

    // Sanity-check the merkle tree
    ASSERT_EQ(MerkleTree::Verify(&info->data[0], info->size_data, &info->merkle[0],
                                 info->size_merkle, 0, info->size_data, digest),
              ZX_OK, "Failed to validate Merkle Tree");

    *out = std::move(info);
    END_HELPER;
}

bool GenerateRandomBlob(fbl::String mount_path, size_t size_data, fbl::unique_ptr<BlobInfo>* out) {
    BEGIN_HELPER;
    ASSERT_TRUE(GenerateBlob(RandomFill, mount_path, size_data, out));
    END_HELPER;
}

bool VerifyContents(int fd, const char* data, size_t size_data) {
    BEGIN_HELPER;
    // Verify the contents of the Blob
    fbl::AllocChecker ac;
    constexpr size_t kReadSize = 8192;
    fbl::unique_ptr<char[]> buffer(new (&ac) char[kReadSize]);
    EXPECT_EQ(ac.check(), true);
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);

    size_t total_read = 0;
    while (total_read != size_data) {
        ssize_t result = read(fd, buffer.get(), kReadSize);
        ASSERT_GT(result, 0);
        ASSERT_EQ(memcmp(buffer.get(), &data[total_read], result), 0);
        total_read += result;
    }

    END_HELPER;
}

} // namespace fs_test_utils
