// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <digest/merkle-tree.h>

#include <stdlib.h>

#include <digest/digest.h>
#include <magenta/assert.h>
#include <magenta/status.h>
#include <unittest/unittest.h>

namespace {

////////////////
// Test support.

// Helper defines for the typical case of checking a mx_status_t
#define BEGIN_TEST_WITH_RC                                                     \
    BEGIN_TEST;                                                                \
    mx_status_t rc
#define ASSERT_ERR(expected, expr)                                             \
    rc = (expr);                                                               \
    ASSERT_EQ(expected, rc, mx_status_get_string(rc))
#define ASSERT_OK(expr) ASSERT_ERR(MX_OK, (expr))

// These unit tests are for the MerkleTree object in ulib/digest.
using digest::Digest;
using digest::MerkleTree;

// The MerkleTree tests below are naturally sensitive to the shape of the Merkle
// tree. These determine those sizes in a consistent way.  The only requirement
// here is that |kSmall * Digest::kLength| should be less than |kNodeSize|.
const uint64_t kNodeSize = MerkleTree::kNodeSize;
const uint64_t kSmall = 8 * kNodeSize;
const uint64_t kLarge = ((kNodeSize / Digest::kLength) + 1) * kNodeSize;
const uint64_t kUnalignedLarge = kLarge + (kNodeSize / 2);

// The hard-coded trees used for testing were created by using sha256sum on
// files generated using echo -ne, dd, and xxd
struct Case {
    size_t data_len;
    size_t tree_len;
    const char digest[(Digest::kLength * 2) + 1];
} kCases[] = {
    {0, 0, "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b"},
    {1, 0, "0967e0f62a104d1595610d272dfab3d2fa2fe07be0eebce13ef5d79db142610e"},
    {kNodeSize / 2, 0,
     "0a90612c255555469dead72c8fdc41eec06dfe04a30a1f2b7c480ff95d20c5ec"},
    {kNodeSize - 1, 0,
     "f2abd690381bab3ce485c814d05c310b22c34a7441418b5c1a002c344a80e730"},
    {kNodeSize, 0,
     "68d131bc271f9c192d4f6dcd8fe61bef90004856da19d0f2f514a7f4098b0737"},
    {kNodeSize + 1, kNodeSize,
     "374781f7d770b6ee9c1a63e186d2d0ccdad10d6aef4fd027e82b1be5b70a2a0c"},
    {kSmall, kNodeSize,
     "f75f59a944d2433bc6830ec243bfefa457704d2aed12f30539cd4f18bf1d62cf"},
    {kLarge, kNodeSize * 3,
     "7d75dfb18bfd48e03b5be4e8e9aeea2f89880cb81c1551df855e0d0a0cc59a67"},
    {kUnalignedLarge, kNodeSize * 3,
     "7577266aa98ce587922fdc668c186e27f3c742fb1b732737153b70ae46973e43"},
};

const size_t kNumCases = sizeof(kCases) / sizeof(struct Case);

// These tests use anonymously scoped globals to reduce the amount of repetitive
// test setup.
uint8_t gData[kUnalignedLarge];
uint8_t gTree[kNodeSize * 3];

////////////////
// Test cases

bool GetTreeLength(void) {
    BEGIN_TEST;
    for (size_t i = 0; i < kNumCases; ++i) {
        MX_DEBUG_ASSERT(kCases[i].data_len <= sizeof(gData));
        MX_DEBUG_ASSERT(kCases[i].tree_len <= sizeof(gTree));
        ASSERT_EQ(kCases[i].tree_len,
                  MerkleTree::GetTreeLength(kCases[i].data_len),
                  "Wrong tree length");
    }
    END_TEST;
}

bool CreateInit(void) {
    BEGIN_TEST_WITH_RC;
    size_t tree_len = MerkleTree::GetTreeLength(kLarge);
    MerkleTree merkleTree;
    ASSERT_OK(merkleTree.CreateInit(kLarge, tree_len));
    END_TEST;
}

bool CreateInitWithoutData(void) {
    BEGIN_TEST_WITH_RC;
    size_t tree_len = MerkleTree::GetTreeLength(kLarge);
    MerkleTree merkleTree;
    ASSERT_OK(merkleTree.CreateInit(0, tree_len));
    ASSERT_OK(merkleTree.CreateInit(0, 0));
    END_TEST;
}

bool CreateInitWithoutTree(void) {
    BEGIN_TEST_WITH_RC;
    MerkleTree merkleTree;
    ASSERT_OK(merkleTree.CreateInit(kNodeSize, 0));
    END_TEST;
}

bool CreateInitTreeTooSmall(void) {
    BEGIN_TEST_WITH_RC;
    size_t tree_len = MerkleTree::GetTreeLength(kLarge);
    MerkleTree merkleTree;
    ASSERT_ERR(MX_ERR_BUFFER_TOO_SMALL,
               merkleTree.CreateInit(kLarge, tree_len - 1));
    END_TEST;
}

bool CreateUpdate(void) {
    BEGIN_TEST_WITH_RC;
    size_t tree_len = MerkleTree::GetTreeLength(kLarge);
    MerkleTree merkleTree;
    ASSERT_OK(merkleTree.CreateInit(kLarge, tree_len));
    ASSERT_OK(merkleTree.CreateUpdate(gData, kLarge, gTree));
    END_TEST;
}

bool CreateUpdateMissingInit(void) {
    BEGIN_TEST_WITH_RC;
    MerkleTree merkleTree;
    ASSERT_ERR(MX_ERR_BAD_STATE, merkleTree.CreateUpdate(gData, kLarge, gTree));
    END_TEST;
}

bool CreateUpdateMissingData(void) {
    BEGIN_TEST_WITH_RC;
    size_t tree_len = MerkleTree::GetTreeLength(kLarge);
    MerkleTree merkleTree;
    ASSERT_OK(merkleTree.CreateInit(kLarge, tree_len));
    ASSERT_ERR(MX_ERR_INVALID_ARGS,
               merkleTree.CreateUpdate(nullptr, kLarge, gTree));
    END_TEST;
}

bool CreateUpdateMissingTree(void) {
    BEGIN_TEST_WITH_RC;
    size_t tree_len = MerkleTree::GetTreeLength(kLarge);
    MerkleTree merkleTree;
    ASSERT_OK(merkleTree.CreateInit(kLarge, tree_len));
    ASSERT_ERR(MX_ERR_INVALID_ARGS,
               merkleTree.CreateUpdate(gData, kLarge, nullptr));
    END_TEST;
}

bool CreateUpdateWithoutData(void) {
    BEGIN_TEST_WITH_RC;
    size_t tree_len = MerkleTree::GetTreeLength(kLarge);
    MerkleTree merkleTree;
    ASSERT_OK(merkleTree.CreateInit(kLarge, tree_len));
    ASSERT_OK(merkleTree.CreateUpdate(gData, 0, gTree));
    ASSERT_OK(merkleTree.CreateUpdate(nullptr, 0, gTree));
    END_TEST;
}

bool CreateUpdateWithoutTree(void) {
    BEGIN_TEST_WITH_RC;
    MerkleTree merkleTree;
    ASSERT_OK(merkleTree.CreateInit(kNodeSize, 0));
    ASSERT_OK(merkleTree.CreateUpdate(gData, kNodeSize, nullptr));
    END_TEST;
}

bool CreateUpdateTooMuchData(void) {
    BEGIN_TEST_WITH_RC;
    size_t tree_len = MerkleTree::GetTreeLength(kLarge);
    MerkleTree merkleTree;
    ASSERT_OK(merkleTree.CreateInit(kLarge, tree_len));
    ASSERT_ERR(MX_ERR_OUT_OF_RANGE,
               merkleTree.CreateUpdate(gData, kLarge + 1, gTree));
    END_TEST;
}

bool CreateFinalMissingInit(void) {
    BEGIN_TEST_WITH_RC;
    MerkleTree merkleTree;
    Digest digest;
    ASSERT_ERR(MX_ERR_BAD_STATE, merkleTree.CreateFinal(gTree, &digest));
    END_TEST;
}

// Used by CreateFinalAll, CreateFinalWithoutData, and CreateFinalWithoutTree
// below
bool CreateFinal(size_t data_len, const char* digest, void* data, void* tree) {
    mx_status_t rc;
    size_t tree_len = MerkleTree::GetTreeLength(data_len);
    MerkleTree merkleTree;
    ASSERT_OK(merkleTree.CreateInit(data_len, tree_len));
    ASSERT_OK(merkleTree.CreateUpdate(data, data_len, tree));
    Digest actual;
    ASSERT_OK(merkleTree.CreateFinal(tree, &actual));
    Digest expected;
    ASSERT_OK(expected.Parse(digest, strlen(digest)));
    ASSERT_TRUE(actual == expected, "Incorrect root digest");
    return true;
}

bool CreateFinalAll(void) {
    BEGIN_TEST;
    for (size_t i = 0; i < kNumCases; ++i) {
        if (!CreateFinal(kCases[i].data_len, kCases[i].digest, gData, gTree)) {
            unittest_printf_critical(
                "CreateFinalAll failed with data length of %zu\n",
                kCases[i].data_len);
        }
    }
    END_TEST;
}

bool CreateFinalWithoutData(void) {
    BEGIN_TEST;
    bool found = false;
    for (size_t i = 0; i < kNumCases; ++i) {
        if (kCases[i].data_len != 0) {
            continue;
        }
        if (!CreateFinal(kCases[i].data_len, kCases[i].digest, nullptr,
                         nullptr)) {
            unittest_printf_critical(
                "CreateFinalWithoutData failed with data length of %zu\n",
                kCases[i].data_len);
        }
        found = true;
    }
    ASSERT_TRUE(found, "Unable to find test cases with length == 0");
    END_TEST;
}

bool CreateFinalWithoutTree(void) {
    BEGIN_TEST;
    bool found = false;
    for (size_t i = 0; i < kNumCases; ++i) {
        if (kCases[i].data_len > kNodeSize) {
            continue;
        }
        if (!CreateFinal(kCases[i].data_len, kCases[i].digest, gData,
                         nullptr)) {
            unittest_printf_critical(
                "CreateFinalWithoutTree failed with data length of %zu\n",
                kCases[i].data_len);
        }
        found = true;
    }
    ASSERT_TRUE(found, "Unable to find test cases with length <= kNodeSize");
    END_TEST;
}

bool CreateFinalMissingDigest(void) {
    BEGIN_TEST_WITH_RC;
    size_t tree_len = MerkleTree::GetTreeLength(kLarge);
    MerkleTree merkleTree;
    ASSERT_OK(merkleTree.CreateInit(kLarge, tree_len));
    ASSERT_OK(merkleTree.CreateUpdate(gData, kLarge, gTree));
    ASSERT_ERR(MX_ERR_INVALID_ARGS, merkleTree.CreateFinal(gTree, nullptr));
    END_TEST;
}

bool CreateFinalIncompleteData(void) {
    BEGIN_TEST_WITH_RC;
    size_t tree_len = MerkleTree::GetTreeLength(kLarge);
    MerkleTree merkleTree;
    ASSERT_OK(merkleTree.CreateInit(kLarge, tree_len));
    ASSERT_OK(merkleTree.CreateUpdate(gData, kLarge - 1, gTree));
    Digest digest;
    ASSERT_ERR(MX_ERR_BAD_STATE, merkleTree.CreateFinal(gTree, &digest));
    END_TEST;
}

// Used by CreateAll below.
bool Create(size_t data_len, const char* digest) {
    mx_status_t rc;
    size_t tree_len = MerkleTree::GetTreeLength(data_len);
    Digest actual;
    ASSERT_OK(MerkleTree::Create(gData, data_len, gTree, tree_len, &actual));
    Digest expected;
    ASSERT_OK(expected.Parse(digest, strlen(digest)));
    ASSERT_TRUE(actual == expected, "Incorrect root digest");
    return true;
}

bool CreateAll(void) {
    BEGIN_TEST;
    for (size_t i = 0; i < kNumCases; ++i) {
        if (!Create(kCases[i].data_len, kCases[i].digest)) {
            unittest_printf_critical(
                "CreateAll failed with data length of %zu\n",
                kCases[i].data_len);
        }
    }
    END_TEST;
}

// Used by CreateFinalCAll below.
bool CreateFinalC(size_t data_len, const char* digest) {
    mx_status_t rc;
    // Init
    size_t tree_len = merkle_tree_get_tree_length(data_len);
    merkle_tree_t* mt = nullptr;
    ASSERT_OK(merkle_tree_create_init(data_len, tree_len, &mt));
    // Update
    size_t i = 0;
    while (data_len - i > kNodeSize) {
        ASSERT_OK(merkle_tree_create_update(mt, gData + i, kNodeSize, gTree));
        i += kNodeSize;
    }
    ASSERT_OK(merkle_tree_create_update(mt, gData + i, data_len - i, gTree));
    // Final
    uint8_t actual[Digest::kLength];
    ASSERT_OK(merkle_tree_create_final(mt, gTree, &actual, sizeof(actual)));
    Digest expected;
    ASSERT_OK(expected.Parse(digest, strlen(digest)));
    ASSERT_TRUE(expected == actual, "Incorrect root digest");
    return true;
}

// See CreateFinalC above.
bool CreateFinalCAll(void) {
    BEGIN_TEST;
    for (size_t i = 0; i < kNumCases; ++i) {
        if (!CreateFinalC(kCases[i].data_len, kCases[i].digest)) {
            unittest_printf_critical("CreateFinalCAll failed with "
                                     "data length of %zu\n",
                                     kCases[i].data_len);
        }
    }
    END_TEST;
}

// Used by CreateCAll below.
bool CreateC(size_t data_len, const char* digest) {
    mx_status_t rc;
    size_t tree_len = merkle_tree_get_tree_length(data_len);
    uint8_t actual[Digest::kLength];
    ASSERT_OK(merkle_tree_create(gData, data_len, gTree, tree_len, &actual,
                                 sizeof(actual)));
    Digest expected;
    ASSERT_OK(expected.Parse(digest, strlen(digest)));
    ASSERT_TRUE(expected == actual, "Incorrect root digest");
    return true;
}

// See CreateC above.
bool CreateCAll(void) {
    BEGIN_TEST;
    for (size_t i = 0; i < kNumCases; ++i) {
        if (!CreateC(kCases[i].data_len, kCases[i].digest)) {
            unittest_printf_critical(
                "CreateCAll failed with data length of %zu\n",
                kCases[i].data_len);
        }
    }
    END_TEST;
}

bool CreateByteByByte(void) {
    BEGIN_TEST_WITH_RC;
    size_t tree_len = MerkleTree::GetTreeLength(kSmall);
    MerkleTree merkleTree;
    ASSERT_OK(merkleTree.CreateInit(kSmall, tree_len));
    for (uint64_t i = 0; i < kSmall; ++i) {
        ASSERT_OK(merkleTree.CreateUpdate(gData + i, 1, gTree));
    }
    Digest actual;
    ASSERT_OK(merkleTree.CreateFinal(gTree, &actual));
    Digest expected;
    ASSERT_OK(MerkleTree::Create(gData, kSmall, gTree, tree_len, &expected));
    ASSERT_TRUE(actual == expected, "Incorrect root digest");
    END_TEST;
}

bool CreateMissingData(void) {
    BEGIN_TEST_WITH_RC;
    size_t tree_len = MerkleTree::GetTreeLength(kSmall);
    Digest digest;
    ASSERT_ERR(MX_ERR_INVALID_ARGS,
               MerkleTree::Create(nullptr, kSmall, gTree, tree_len, &digest));
    END_TEST;
}

bool CreateMissingTree(void) {
    BEGIN_TEST_WITH_RC;
    Digest digest;
    ASSERT_ERR(MX_ERR_INVALID_ARGS,
               MerkleTree::Create(gData, kSmall, nullptr, kNodeSize, &digest));
    END_TEST;
}

bool CreateTreeTooSmall(void) {
    BEGIN_TEST_WITH_RC;
    Digest digest;
    ASSERT_ERR(MX_ERR_BUFFER_TOO_SMALL,
               MerkleTree::Create(gData, kSmall, nullptr, 0, &digest));
    ASSERT_ERR(
        MX_ERR_BUFFER_TOO_SMALL,
        MerkleTree::Create(gData, kNodeSize * 257, gTree, kNodeSize, &digest));
    END_TEST;
}

// Used by VerifyAll below.
bool Verify(size_t data_len) {
    mx_status_t rc;
    size_t tree_len = MerkleTree::GetTreeLength(data_len);
    Digest digest;
    ASSERT_OK(MerkleTree::Create(gData, data_len, gTree, tree_len, &digest));
    ASSERT_OK(MerkleTree::Verify(gData, data_len, gTree, tree_len, 0, data_len,
                                 digest));
    return true;
}

// See Verify above.
bool VerifyAll(void) {
    BEGIN_TEST;
    for (size_t i = 0; i < kNumCases; ++i) {
        if (!Verify(kCases[i].data_len)) {
            unittest_printf_critical(
                "VerifyAll failed with data length of %zu\n",
                kCases[i].data_len);
        }
    }
    END_TEST;
}

// Used by VerifyCAll below.
bool VerifyC(size_t data_len) {
    mx_status_t rc;
    size_t tree_len = merkle_tree_get_tree_length(data_len);
    uint8_t digest[Digest::kLength];
    ASSERT_OK(merkle_tree_create(gData, data_len, gTree, tree_len, digest,
                                 sizeof(digest)));
    ASSERT_OK(merkle_tree_verify(gData, data_len, gTree, tree_len, 0, data_len,
                                 digest, sizeof(digest)));
    return true;
}

// See VerifyC above.
bool VerifyCAll(void) {
    BEGIN_TEST;
    for (size_t i = 0; i < kNumCases; ++i) {
        if (!VerifyC(kCases[i].data_len)) {
            unittest_printf_critical(
                "VerifyCAll failed with data length of %zu\n",
                kCases[i].data_len);
        }
    }
    END_TEST;
}

bool VerifyNodeByNode(void) {
    BEGIN_TEST_WITH_RC;
    size_t tree_len = MerkleTree::GetTreeLength(kSmall);
    Digest digest;
    ASSERT_OK(MerkleTree::Create(gData, kSmall, gTree, tree_len, &digest));
    for (uint64_t i = 0; i < kSmall; i += kNodeSize) {
        ASSERT_OK(MerkleTree::Verify(gData, kSmall, gTree, tree_len, i,
                                     kNodeSize, digest));
    }
    END_TEST;
}

bool VerifyMissingData(void) {
    BEGIN_TEST_WITH_RC;
    size_t tree_len = MerkleTree::GetTreeLength(kSmall);
    Digest digest;
    ASSERT_OK(MerkleTree::Create(gData, kSmall, gTree, tree_len, &digest));
    ASSERT_ERR(MX_ERR_INVALID_ARGS,
               MerkleTree::Verify(nullptr, kSmall, gTree, tree_len, 0, kSmall,
                                  digest));
    END_TEST;
}

bool VerifyMissingTree(void) {
    BEGIN_TEST_WITH_RC;
    size_t tree_len = MerkleTree::GetTreeLength(kSmall);
    Digest digest;
    ASSERT_OK(MerkleTree::Create(gData, kSmall, gTree, tree_len, &digest));
    ASSERT_ERR(MX_ERR_INVALID_ARGS,
               MerkleTree::Verify(gData, kNodeSize + 1, nullptr, tree_len, 0,
                                  kNodeSize, digest));
    END_TEST;
}

bool VerifyUnalignedTreeLength(void) {
    BEGIN_TEST_WITH_RC;
    size_t tree_len = MerkleTree::GetTreeLength(kSmall);
    Digest digest;
    ASSERT_OK(MerkleTree::Create(gData, kSmall, gTree, tree_len, &digest));
    ASSERT_OK(MerkleTree::Verify(gData, kSmall, gTree, tree_len + 1, 0, kSmall,
                                 digest));
    END_TEST;
}

bool VerifyUnalignedDataLength(void) {
    BEGIN_TEST_WITH_RC;
    size_t tree_len = MerkleTree::GetTreeLength(kSmall);
    Digest digest;
    ASSERT_OK(MerkleTree::Create(gData, kSmall, gTree, tree_len, &digest));
    ASSERT_OK(MerkleTree::Verify(gData, kSmall - 1, gTree, tree_len, 0,
                                 kNodeSize, digest));
    END_TEST;
}

bool VerifyTreeTooSmall(void) {
    BEGIN_TEST_WITH_RC;
    size_t tree_len = MerkleTree::GetTreeLength(kSmall);
    Digest digest;
    ASSERT_OK(MerkleTree::Create(gData, kSmall, gTree, tree_len, &digest));
    tree_len = MerkleTree::GetTreeLength(kSmall);
    ASSERT_ERR(MX_ERR_BUFFER_TOO_SMALL,
               MerkleTree::Verify(gData, kSmall, gTree, tree_len - 1, 0, kSmall,
                                  digest));
    END_TEST;
}

bool VerifyUnalignedOffset(void) {
    BEGIN_TEST_WITH_RC;
    size_t tree_len = MerkleTree::GetTreeLength(kSmall);
    Digest digest;
    ASSERT_OK(MerkleTree::Create(gData, kSmall, gTree, tree_len, &digest));
    ASSERT_OK(MerkleTree::Verify(gData, kSmall, gTree, tree_len, kNodeSize - 1,
                                 kNodeSize, digest));
    END_TEST;
}

bool VerifyUnalignedLength(void) {
    BEGIN_TEST_WITH_RC;
    size_t tree_len = MerkleTree::GetTreeLength(kSmall);
    Digest digest;
    ASSERT_OK(MerkleTree::Create(gData, kSmall, gTree, tree_len, &digest));
    ASSERT_OK(MerkleTree::Verify(gData, kSmall, gTree, tree_len, 0, kSmall - 1,
                                 digest));
    END_TEST;
}

bool VerifyOutOfBounds(void) {
    BEGIN_TEST_WITH_RC;
    size_t tree_len = MerkleTree::GetTreeLength(kSmall);
    Digest digest;
    ASSERT_OK(MerkleTree::Create(gData, kSmall, gTree, tree_len, &digest));
    ASSERT_ERR(MX_ERR_OUT_OF_RANGE,
               MerkleTree::Verify(gData, kSmall, gTree, tree_len,
                                  kSmall - kNodeSize, kNodeSize * 2, digest));
    END_TEST;
}

bool VerifyZeroLength(void) {
    BEGIN_TEST_WITH_RC;
    size_t tree_len = MerkleTree::GetTreeLength(kSmall);
    Digest digest;
    ASSERT_OK(MerkleTree::Create(gData, kSmall, gTree, tree_len, &digest));
    ASSERT_OK(MerkleTree::Verify(gData, kSmall, gTree, tree_len, 0, 0, digest));
    END_TEST;
}

bool VerifyBadRoot(void) {
    BEGIN_TEST_WITH_RC;
    size_t tree_len = MerkleTree::GetTreeLength(kLarge);
    Digest digest;
    ASSERT_OK(MerkleTree::Create(gData, kLarge, gTree, tree_len, &digest));
    // Modify digest
    char str[(Digest::kLength * 2) + 1];
    ASSERT_OK(digest.ToString(str, sizeof(str)));
    str[0] = (str[0] == '0' ? '1' : '0');
    rc = digest.Parse(str, strlen(str));
    ASSERT_EQ(rc, MX_OK, mx_status_get_string(rc));
    // Verify
    ASSERT_ERR(
        MX_ERR_IO_DATA_INTEGRITY,
        MerkleTree::Verify(gData, kLarge, gTree, tree_len, 0, kLarge, digest));
    END_TEST;
}

// TODO
bool VerifyGoodPartOfBadTree(void) {
    BEGIN_TEST_WITH_RC;
    size_t tree_len = MerkleTree::GetTreeLength(kLarge);
    Digest digest;
    ASSERT_OK(MerkleTree::Create(gData, kLarge, gTree, tree_len, &digest));
    gTree[0] ^= 1;
    ASSERT_OK(MerkleTree::Verify(gData, kLarge, gTree, tree_len,
                                 kLarge - kNodeSize, kNodeSize, digest));
    END_TEST;
}

bool VerifyBadTree(void) {
    BEGIN_TEST_WITH_RC;
    size_t tree_len = MerkleTree::GetTreeLength(kLarge);
    Digest digest;
    ASSERT_OK(MerkleTree::Create(gData, kLarge, gTree, tree_len, &digest));
    gTree[0] ^= 1;
    ASSERT_ERR(
        MX_ERR_IO_DATA_INTEGRITY,
        MerkleTree::Verify(gData, kLarge, gTree, tree_len, 0, 1, digest));
    END_TEST;
}

bool VerifyGoodPartOfBadLeaves(void) {
    BEGIN_TEST_WITH_RC;
    size_t tree_len = MerkleTree::GetTreeLength(kSmall);
    Digest digest;
    ASSERT_OK(MerkleTree::Create(gData, kSmall, gTree, tree_len, &digest));
    gData[0] ^= 1;
    ASSERT_OK(MerkleTree::Verify(gData, kSmall, gTree, tree_len, kNodeSize,
                                 kSmall - kNodeSize, digest));
    END_TEST;
}

bool VerifyBadLeaves(void) {
    BEGIN_TEST_WITH_RC;
    size_t tree_len = MerkleTree::GetTreeLength(kSmall);
    Digest digest;
    ASSERT_OK(MerkleTree::Create(gData, kSmall, gTree, tree_len, &digest));
    gData[0] ^= 1;
    ASSERT_ERR(
        MX_ERR_IO_DATA_INTEGRITY,
        MerkleTree::Verify(gData, kSmall, gTree, tree_len, 0, kSmall, digest));
    END_TEST;
}

bool CreateAndVerifyHugePRNGData(void) {
    BEGIN_TEST_WITH_RC;
    Digest digest;
    uint8_t buffer[Digest::kLength];
    for (size_t data_len = kNodeSize; data_len <= sizeof(gData);
         data_len <<= 1) {
        // Generate random data
        for (uint64_t i = 0; i < data_len; ++i) {
            gData[i] = static_cast<uint8_t>(rand());
        }
        // Create the Merkle tree
        size_t tree_len = MerkleTree::GetTreeLength(data_len);
        ASSERT_OK(
            MerkleTree::Create(gData, data_len, gTree, tree_len, &digest));
        // Randomly pick one of the four cases below.
        uint64_t n = (rand() % 16) + 1;
        switch (rand() % 4) {
        case 1:
            ASSERT_OK(digest.CopyTo(buffer, sizeof(buffer)));
            // Flip bits in root digest
            for (uint64_t i = 0; i < n; ++i) {
                uint8_t tmp = static_cast<uint8_t>(rand()) % 8;
                buffer[rand() % Digest::kLength] ^=
                    static_cast<uint8_t>(1 << tmp);
            }
            digest = buffer;
            ASSERT_ERR(MX_ERR_IO_DATA_INTEGRITY,
                       MerkleTree::Verify(gData, data_len, gTree, tree_len, 0,
                                          data_len, digest));
            break;
        case 2:
            // Flip bit in data
            for (uint64_t i = 0; i < n; ++i) {
                uint8_t tmp = static_cast<uint8_t>(rand()) % 8;
                gData[rand() % data_len] ^= static_cast<uint8_t>(1 << tmp);
            }
            ASSERT_ERR(MX_ERR_IO_DATA_INTEGRITY,
                       MerkleTree::Verify(gData, data_len, gTree, tree_len, 0,
                                          data_len, digest));
            break;
        case 3:
            // Flip bit in tree (if large enough to have a tree)
            for (uint64_t i = 0; i < n && tree_len > 0; ++i) {
                uint8_t tmp = static_cast<uint8_t>(rand()) % 8;
                gTree[rand() % tree_len] ^= static_cast<uint8_t>(1 << tmp);
            }
            rc = MerkleTree::Verify(gData, data_len, gTree, tree_len, 0,
                                    data_len, digest);

            if (tree_len <= kNodeSize) {
                ASSERT_EQ(rc, MX_OK, mx_status_get_string(rc));
            } else {
                ASSERT_EQ(rc, MX_ERR_IO_DATA_INTEGRITY,
                          mx_status_get_string(rc));
            }
            break;
        default:
            // Normal verification without modification
            ASSERT_OK(MerkleTree::Verify(gData, data_len, gTree, tree_len, 0,
                                         data_len, digest));
            break;
        }
    }
    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(MerkleTreeTests)
// Do this global setup once
memset(gData, 0xff, sizeof(gData));
RUN_TEST(GetTreeLength)
RUN_TEST(CreateInit)
RUN_TEST(CreateInitWithoutData)
RUN_TEST(CreateInitWithoutTree)
RUN_TEST(CreateInitTreeTooSmall)
RUN_TEST(CreateUpdate)
RUN_TEST(CreateUpdateMissingInit)
RUN_TEST(CreateUpdateMissingData)
RUN_TEST(CreateUpdateMissingTree)
RUN_TEST(CreateUpdateWithoutData)
RUN_TEST(CreateUpdateWithoutTree)
RUN_TEST(CreateUpdateTooMuchData)
RUN_TEST(CreateFinalMissingInit)
RUN_TEST(CreateFinalAll)
RUN_TEST(CreateFinalWithoutData)
RUN_TEST(CreateFinalWithoutTree)
RUN_TEST(CreateFinalMissingDigest)
RUN_TEST(CreateFinalIncompleteData)
RUN_TEST(CreateAll)
RUN_TEST(CreateFinalCAll)
RUN_TEST(CreateCAll)
RUN_TEST(CreateByteByByte)
RUN_TEST(CreateMissingData)
RUN_TEST(CreateMissingTree)
RUN_TEST(CreateTreeTooSmall)
RUN_TEST(VerifyAll)
RUN_TEST(VerifyCAll)
RUN_TEST(VerifyNodeByNode)
RUN_TEST(VerifyMissingData)
RUN_TEST(VerifyMissingTree)
RUN_TEST(VerifyUnalignedTreeLength)
RUN_TEST(VerifyUnalignedDataLength)
RUN_TEST(VerifyTreeTooSmall)
RUN_TEST(VerifyUnalignedOffset)
RUN_TEST(VerifyUnalignedLength)
RUN_TEST(VerifyOutOfBounds)
RUN_TEST(VerifyZeroLength)
RUN_TEST(VerifyBadRoot)
RUN_TEST(VerifyGoodPartOfBadTree)
RUN_TEST(VerifyBadTree)
RUN_TEST(VerifyGoodPartOfBadLeaves)
RUN_TEST(VerifyBadLeaves)
RUN_TEST(CreateAndVerifyHugePRNGData)
END_TEST_CASE(MerkleTreeTests)
