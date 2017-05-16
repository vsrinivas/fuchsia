// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <merkle/digest.h>
#include <merkle/tree.h>

#include <stdlib.h>

#include <magenta/assert.h>
#include <magenta/status.h>
#include <unittest/unittest.h>

namespace {

////////////////
// Test support.

// These unit tests are for the objects in ulib/merkle: Digest and Tree.
using merkle::Tree;
using merkle::Digest;

// The Tree tests below are naturally sensitive to the shape of the Merkle tree.
// These determine those sizes in a consistent way.  The only requirement here
// is that |kSmall * Digest::kLength| should be less than |kNodeSize|.
const size_t kNodeSize = Tree::kNodeSize;
const size_t kSmall = 8 * kNodeSize;
const size_t kLarge = ((kNodeSize / Digest::kLength) + 1) * kNodeSize;
const size_t kUnaligned = kLarge + (kNodeSize / 2);

// The hard-coded trees used for testing were created by using sha256sum on
// files generated using echo -ne, dd, and xxd
const char* kNoDataDigest =
    "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b";
const char* kOneNodeDigest =
    "68d131bc271f9c192d4f6dcd8fe61bef90004856da19d0f2f514a7f4098b0737";
const char* kSmallDigest =
    "f75f59a944d2433bc6830ec243bfefa457704d2aed12f30539cd4f18bf1d62cf";
const char* kLargeDigest =
    "7d75dfb18bfd48e03b5be4e8e9aeea2f89880cb81c1551df855e0d0a0cc59a67";
const char* kUnalignedDigest =
    "7577266aa98ce587922fdc668c186e27f3c742fb1b732737153b70ae46973e43";

// These tests use anonymously scoped globals to reduce the amount of repetitive
// test setup.
size_t gDataLen;
uint8_t gData[1 << 24];
size_t gTreeLen;
uint8_t gTree[1 << 24];
Digest gDigest;
uint64_t gOffset;
size_t gLength;

// Sets up the global variables to represent a data blob of |num| nodes,
// completely filled with 0's.
void InitData(size_t length) {
    memset(gData, 0xff, sizeof(gData));
    gDataLen = length;
    MX_DEBUG_ASSERT(gDataLen <= sizeof(gData));
    gTreeLen = sizeof(gTree);
    if (length >= kNodeSize * 3) {
        gOffset = gDataLen - (kNodeSize * 3);
        gLength = kNodeSize * 2;
    } else {
        gOffset = 0;
        gLength = gDataLen;
    }
}

////////////////
// Test cases

bool GetTreeLength(void) {
    BEGIN_TEST;
    Tree merkleTree;
    ASSERT_EQ(0, merkleTree.GetTreeLength(0),
              "Wrong tree length for empty tree");
    ASSERT_EQ(0, merkleTree.GetTreeLength(1), "Wrong tree length for 1 byte");
    ASSERT_EQ(0, merkleTree.GetTreeLength(kNodeSize),
              "Wrong tree length for 1 node");
    ASSERT_EQ(kNodeSize, merkleTree.GetTreeLength(kNodeSize + 1),
              "Wrong tree length for 2 nodes");
    ASSERT_EQ(kNodeSize,
              merkleTree.GetTreeLength(kNodeSize * kNodeSize / Digest::kLength),
              "Wrong tree length for 1 node of digests");
    ASSERT_EQ(
        kNodeSize * 3,
        merkleTree.GetTreeLength((kNodeSize * kNodeSize / Digest::kLength) + 1),
        "Wrong tree length for 2 nodes of digests");
    END_TEST;
}

bool CreateInit(void) {
    BEGIN_TEST;
    InitData(kLarge);
    Tree merkleTree;
    mx_status_t rc = merkleTree.CreateInit(gDataLen, gTree, gTreeLen);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    END_TEST;
}

bool CreateInitMissingTree(void) {
    BEGIN_TEST;
    InitData(kLarge);
    Tree merkleTree;
    mx_status_t rc = merkleTree.CreateInit(gDataLen, nullptr, gTreeLen);
    ASSERT_EQ(rc, ERR_INVALID_ARGS, mx_status_get_string(rc));
    END_TEST;
}

bool CreateInitWithoutData(void) {
    BEGIN_TEST;
    InitData(kLarge);
    Tree merkleTree;
    mx_status_t rc = merkleTree.CreateInit(0, gTree, gTreeLen);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.CreateInit(0, gTree, 0);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.CreateInit(0, nullptr, 0);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    END_TEST;
}

bool CreateInitWithoutTree(void) {
    BEGIN_TEST;
    InitData(kSmall);
    Tree merkleTree;
    mx_status_t rc = merkleTree.CreateInit(kNodeSize, nullptr, 0);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    END_TEST;
}

bool CreateInitTreeTooSmall(void) {
    BEGIN_TEST;
    InitData(kLarge);
    Tree merkleTree;
    gTreeLen = merkleTree.GetTreeLength(gDataLen);
    mx_status_t rc = merkleTree.CreateInit(gDataLen, gTree, gTreeLen - 1);
    ASSERT_EQ(rc, ERR_BUFFER_TOO_SMALL, mx_status_get_string(rc));
    END_TEST;
}

bool CreateUpdate(void) {
    BEGIN_TEST;
    InitData(kLarge);
    Tree merkleTree;
    mx_status_t rc = merkleTree.CreateInit(gDataLen, gTree, gTreeLen);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.CreateUpdate(gData, gLength, gTree);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    END_TEST;
}

bool CreateUpdateMissingInit(void) {
    BEGIN_TEST;
    InitData(kLarge);
    Tree merkleTree;
    mx_status_t rc = merkleTree.CreateUpdate(gData, gLength, gTree);
    ASSERT_EQ(rc, ERR_BAD_STATE, mx_status_get_string(rc));
    END_TEST;
}

bool CreateUpdateMissingData(void) {
    BEGIN_TEST;
    InitData(kLarge);
    Tree merkleTree;
    mx_status_t rc = merkleTree.CreateInit(gDataLen, gTree, gTreeLen);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.CreateUpdate(nullptr, gLength, gTree);
    ASSERT_EQ(rc, ERR_INVALID_ARGS, mx_status_get_string(rc));
    END_TEST;
}

bool CreateUpdateMissingTree(void) {
    BEGIN_TEST;
    InitData(kLarge);
    Tree merkleTree;
    mx_status_t rc = merkleTree.CreateInit(gDataLen, gTree, gTreeLen);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.CreateUpdate(gData, gLength, nullptr);
    ASSERT_EQ(rc, ERR_INVALID_ARGS, mx_status_get_string(rc));
    END_TEST;
}

bool CreateUpdateWithoutData(void) {
    BEGIN_TEST;
    InitData(kLarge);
    Tree merkleTree;
    mx_status_t rc = merkleTree.CreateInit(gDataLen, gTree, gTreeLen);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.CreateUpdate(gData, 0, gTree);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.CreateUpdate(nullptr, 0, gTree);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    END_TEST;
}

bool CreateUpdateWithoutTree(void) {
    BEGIN_TEST;
    InitData(kSmall);
    Tree merkleTree;
    mx_status_t rc = merkleTree.CreateInit(kNodeSize, nullptr, 0);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.CreateUpdate(gData, kNodeSize, nullptr);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    END_TEST;
}

bool CreateUpdateTooMuchData(void) {
    BEGIN_TEST;
    InitData(kLarge);
    Tree merkleTree;
    mx_status_t rc = merkleTree.CreateInit(gDataLen, gTree, gTreeLen);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.CreateUpdate(gData, gDataLen + 1, gTree);
    ASSERT_EQ(rc, ERR_BUFFER_TOO_SMALL, mx_status_get_string(rc));
    END_TEST;
}

bool CreateFinal(void) {
    BEGIN_TEST;
    InitData(kSmall);
    Tree merkleTree;
    mx_status_t rc = merkleTree.CreateInit(gDataLen, gTree, gTreeLen);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.CreateUpdate(gData, gDataLen, gTree);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.CreateFinal(gTree, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    Digest expected;
    rc = expected.Parse(kSmallDigest, strlen(kSmallDigest));
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    ASSERT_TRUE(gDigest == expected, "Incorrect root digest");
    END_TEST;
}

bool CreateFinalMissingInit(void) {
    BEGIN_TEST;
    InitData(kLarge);
    Tree merkleTree;
    mx_status_t rc = merkleTree.CreateFinal(gTree, &gDigest);
    ASSERT_EQ(rc, ERR_BAD_STATE, mx_status_get_string(rc));
    END_TEST;
}

bool CreateFinalWithoutData(void) {
    BEGIN_TEST;
    InitData(kSmall);
    Tree merkleTree;
    mx_status_t rc = merkleTree.CreateInit(0, gTree, gTreeLen);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.CreateFinal(gTree, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    Digest expected;
    rc = expected.Parse(kNoDataDigest, strlen(kNoDataDigest));
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    ASSERT_TRUE(gDigest == expected, "Incorrect root digest");
    END_TEST;
}

bool CreateFinalWithoutTree(void) {
    BEGIN_TEST;
    InitData(kSmall);
    Tree merkleTree;
    mx_status_t rc = merkleTree.CreateInit(kNodeSize, nullptr, 0);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.CreateUpdate(gData, kNodeSize, nullptr);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.CreateFinal(nullptr, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    Digest expected;
    rc = expected.Parse(kOneNodeDigest, strlen(kOneNodeDigest));
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    ASSERT_TRUE(gDigest == expected, "Incorrect root digest");
    END_TEST;
}

bool CreateFinalMissingDigest(void) {
    BEGIN_TEST;
    InitData(kLarge);
    Tree merkleTree;
    mx_status_t rc = merkleTree.CreateInit(gDataLen, gTree, gTreeLen);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.CreateUpdate(gData, gDataLen, gTree);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.CreateFinal(gTree, nullptr);
    ASSERT_EQ(rc, ERR_INVALID_ARGS, mx_status_get_string(rc));
    END_TEST;
}

bool CreateFinalIncompleteData(void) {
    BEGIN_TEST;
    InitData(kLarge);
    Tree merkleTree;
    mx_status_t rc = merkleTree.CreateInit(gDataLen, gTree, gTreeLen);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.CreateUpdate(gData, gDataLen - 1, gTree);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.CreateFinal(gTree, &gDigest);
    ASSERT_EQ(rc, ERR_BAD_STATE, mx_status_get_string(rc));
    END_TEST;
}

bool Create(void) {
    BEGIN_TEST;
    InitData(kLarge);
    Tree merkleTree;
    mx_status_t rc =
        merkleTree.Create(gData, gDataLen, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    Digest expected;
    rc = expected.Parse(kLargeDigest, strlen(kLargeDigest));
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    ASSERT_TRUE(gDigest == expected, "Incorrect root digest");
    END_TEST;
}

bool CreateCWrappers(void) {
    BEGIN_TEST;
    InitData(kSmall);
    gTreeLen = merkle_tree_length(gDataLen);
    uint8_t digest[Digest::kLength];
    mx_status_t rc = merkle_tree_create(gData, gDataLen, gTree, gTreeLen,
                                        digest, sizeof(digest));
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    Digest expected;
    rc = expected.Parse(kSmallDigest, strlen(kSmallDigest));
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    ASSERT_TRUE(expected == digest, "Incorrect root digest");
    merkle_tree_t* tree = nullptr;
    rc = merkle_tree_init(gDataLen, &tree);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    for (uint64_t i = 0; i < gDataLen; i += kNodeSize) {
        rc = merkle_tree_update(tree, gData + i, kNodeSize);
        ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    }
    rc = merkle_tree_final(tree, digest, sizeof(digest));
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    ASSERT_TRUE(expected == digest, "Incorrect root digest");
    END_TEST;
}

bool CreateByteByByte(void) {
    BEGIN_TEST;
    InitData(kSmall);
    Tree merkleTree;
    mx_status_t rc = merkleTree.CreateInit(gDataLen, gTree, gTreeLen);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    for (size_t i = 0; i < gDataLen; ++i) {
        rc = merkleTree.CreateUpdate(gData + i, 1, gTree);
        ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    }
    rc = merkleTree.CreateFinal(gTree, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    Digest expected;
    rc = expected.Parse(kSmallDigest, strlen(kSmallDigest));
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    ASSERT_TRUE(gDigest == expected, "Incorrect root digest");
    END_TEST;
}

bool CreateWithoutData(void) {
    BEGIN_TEST;
    InitData(kSmall);
    Tree merkleTree;
    mx_status_t rc = merkleTree.Create(nullptr, 0, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.Create(gData, 0, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    Digest expected;
    rc = expected.Parse(kNoDataDigest, strlen(kNoDataDigest));
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    ASSERT_TRUE(gDigest == expected, "Incorrect root digest");
    END_TEST;
}

bool CreateWithoutTree(void) {
    BEGIN_TEST;
    InitData(kSmall);
    Tree merkleTree;
    mx_status_t rc = merkleTree.Create(gData, kNodeSize, nullptr, 0, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.Create(gData, kNodeSize, gTree, 0, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    Digest expected;
    rc = expected.Parse(kOneNodeDigest, strlen(kOneNodeDigest));
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    ASSERT_TRUE(gDigest == expected, "Incorrect root digest");
    END_TEST;
}

bool CreateMissingData(void) {
    BEGIN_TEST;
    InitData(kSmall);
    Tree merkleTree;
    mx_status_t rc =
        merkleTree.Create(nullptr, gDataLen, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, ERR_INVALID_ARGS, mx_status_get_string(rc));
    END_TEST;
}

bool CreateMissingTree(void) {
    BEGIN_TEST;
    InitData(kSmall);
    Tree merkleTree;
    mx_status_t rc =
        merkleTree.Create(gData, gDataLen, nullptr, kNodeSize, &gDigest);
    ASSERT_EQ(rc, ERR_INVALID_ARGS, mx_status_get_string(rc));
    END_TEST;
}

bool CreateTreeTooSmall(void) {
    BEGIN_TEST;
    InitData(kSmall);
    Tree merkleTree;
    mx_status_t rc = merkleTree.Create(gData, gDataLen, nullptr, 0, &gDigest);
    ASSERT_EQ(rc, ERR_BUFFER_TOO_SMALL, mx_status_get_string(rc));
    rc = merkleTree.Create(gData, kNodeSize * 257, gTree, kNodeSize, &gDigest);
    ASSERT_EQ(rc, ERR_BUFFER_TOO_SMALL, mx_status_get_string(rc));
    END_TEST;
}

bool CreateDataUnaligned(void) {
    BEGIN_TEST;
    InitData(kUnaligned);
    Tree merkleTree;
    mx_status_t rc =
        merkleTree.Create(gData, gDataLen, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    Digest expected;
    rc = expected.Parse(kUnalignedDigest, strlen(kUnalignedDigest));
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    ASSERT_TRUE(gDigest == expected, "Incorrect root digest");
    END_TEST;
}

bool SetRanges(void) {
    BEGIN_TEST;
    Tree merkleTree;
    InitData(kLarge);
    mx_status_t rc =
        merkleTree.Create(gData, gDataLen, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.SetRanges(gDataLen, gOffset, gLength);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    const auto& ranges = merkleTree.ranges();
    ASSERT_EQ(ranges.size(), 2, "number of ranges");
    ASSERT_EQ(ranges[0].offset, 0, "offset 0");
    ASSERT_EQ(ranges[0].length, kNodeSize, "length 0");
    ASSERT_EQ(ranges[1].offset, kNodeSize * 2, "offset 1");
    ASSERT_EQ(ranges[1].length, kNodeSize, "length 1");
    END_TEST;
}

bool SetRangesEmpty(void) {
    BEGIN_TEST;
    Tree merkleTree;
    InitData(kLarge);
    mx_status_t rc =
        merkleTree.Create(gData, gDataLen, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.SetRanges(gDataLen, gOffset, 0);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    const auto& ranges = merkleTree.ranges();
    ASSERT_EQ(ranges.size(), 2, "number of ranges");
    ASSERT_EQ(ranges[0].length, 0, "length 0");
    ASSERT_EQ(ranges[1].length, 0, "length 1");
    END_TEST;
}

bool SetRangesFull(void) {
    BEGIN_TEST;
    Tree merkleTree;
    InitData(kLarge);
    mx_status_t rc =
        merkleTree.Create(gData, gDataLen, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.SetRanges(gDataLen, 0, gDataLen);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    const auto& ranges = merkleTree.ranges();
    ASSERT_EQ(ranges.size(), 2, "number of ranges");
    ASSERT_EQ(ranges[0].offset, 0, "offset 0");
    ASSERT_EQ(ranges[0].length, kNodeSize * 2, "length 0");
    ASSERT_EQ(ranges[1].offset, kNodeSize * 2, "offset 1");
    ASSERT_EQ(ranges[1].length, kNodeSize, "length 1");
    END_TEST;
}

bool SetRangesUnalignedOffset(void) {
    BEGIN_TEST;
    Tree merkleTree;
    InitData(kUnaligned);
    mx_status_t rc =
        merkleTree.Create(gData, gDataLen, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.SetRanges(gDataLen, gOffset + 1, gLength);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    const auto& ranges = merkleTree.ranges();
    ASSERT_EQ(ranges.size(), 2, "number of ranges");
    ASSERT_EQ(ranges[0].offset, 0, "offset 0");
    ASSERT_EQ(ranges[0].length, kNodeSize * 2, "length 0");
    ASSERT_EQ(ranges[1].offset, kNodeSize * 2, "offset 1");
    ASSERT_EQ(ranges[1].length, kNodeSize, "length 1");
    END_TEST;
}

bool SetRangesUnalignedLength(void) {
    BEGIN_TEST;
    Tree merkleTree;
    InitData(kUnaligned);
    mx_status_t rc =
        merkleTree.Create(gData, gDataLen, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.SetRanges(gDataLen, gOffset, gLength + 1);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.SetRanges(gDataLen, gOffset, gDataLen - gOffset);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    const auto& ranges = merkleTree.ranges();
    ASSERT_EQ(ranges.size(), 2, "number of ranges");
    ASSERT_EQ(ranges[0].offset, 0, "offset 0");
    ASSERT_EQ(ranges[0].length, kNodeSize * 2, "length 0");
    ASSERT_EQ(ranges[1].offset, kNodeSize * 2, "offset 1");
    ASSERT_EQ(ranges[1].length, kNodeSize, "length 1");
    END_TEST;
}

bool SetRangesOutOfBounds(void) {
    BEGIN_TEST;
    Tree merkleTree;
    InitData(kLarge);
    mx_status_t rc =
        merkleTree.Create(gData, gDataLen, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.SetRanges(gDataLen, gDataLen - kNodeSize, kNodeSize * 2);
    ASSERT_EQ(rc, ERR_INVALID_ARGS, mx_status_get_string(rc));
    END_TEST;
}

bool Verify(void) {
    BEGIN_TEST;
    Tree merkleTree;
    auto& data_failures = merkleTree.data_failures();
    auto& tree_failures = merkleTree.tree_failures();
    InitData(kSmall);
    mx_status_t rc =
        merkleTree.Create(gData, gDataLen, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.Verify(gData, gDataLen, gTree, gTreeLen, gOffset, gLength,
                           gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    ASSERT_EQ(data_failures.size(), 0, "Wrong number of data_failures");
    ASSERT_EQ(tree_failures.size(), 0, "Wrong number of tree_failures");
    END_TEST;
}

bool VerifyCWrapper(void) {
    BEGIN_TEST;
    InitData(kSmall);
    size_t gTreeLen = merkle_tree_length(gDataLen);
    uint8_t digest[Digest::kLength];
    mx_status_t rc = merkle_tree_create(gData, gDataLen, gTree, gTreeLen,
                                        digest, sizeof(digest));
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = gDigest.CopyTo(digest, sizeof(digest));
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkle_tree_verify(gData, gDataLen, gTree, gTreeLen, gOffset, gLength,
                            digest, sizeof(digest));
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    END_TEST;
}

bool VerifyNodeByNode(void) {
    BEGIN_TEST;
    Tree merkleTree;
    InitData(kSmall);
    mx_status_t rc =
        merkleTree.Create(gData, gDataLen, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    for (size_t i = 0; i < gDataLen; i += kNodeSize) {
        rc = merkleTree.Verify(gData, gDataLen, gTree, gTreeLen, i, kNodeSize,
                               gDigest);
        ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    }
    END_TEST;
}

bool VerifyWithoutData(void) {
    BEGIN_TEST;
    Tree merkleTree;
    auto& data_failures = merkleTree.data_failures();
    auto& tree_failures = merkleTree.tree_failures();
    InitData(kSmall);
    mx_status_t rc = merkleTree.Create(gData, 0, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = gDigest.Parse(kNoDataDigest, strlen(kNoDataDigest));
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.Verify(gData, 0, gTree, gTreeLen, 0, 0, gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    ASSERT_EQ(data_failures.size(), 0, "Wrong number of data_failures");
    ASSERT_EQ(tree_failures.size(), 0, "Wrong number of tree_failures");
    END_TEST;
}

bool VerifyWithoutTree(void) {
    BEGIN_TEST;
    Tree merkleTree;
    auto& data_failures = merkleTree.data_failures();
    auto& tree_failures = merkleTree.tree_failures();
    InitData(kSmall);
    mx_status_t rc =
        merkleTree.Create(gData, kNodeSize, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = gDigest.Parse(kOneNodeDigest, strlen(kOneNodeDigest));
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.Verify(gData, kNodeSize, nullptr, 0, 0, kNodeSize, gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    ASSERT_EQ(data_failures.size(), 0, "Wrong number of data_failures");
    ASSERT_EQ(tree_failures.size(), 0, "Wrong number of tree_failures");
    END_TEST;
}

bool VerifyMissingData(void) {
    BEGIN_TEST;
    Tree merkleTree;
    InitData(kSmall);
    mx_status_t rc =
        merkleTree.Create(gData, gDataLen, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.Verify(nullptr, gDataLen, gTree, gTreeLen, gOffset, gLength,
                           gDigest);
    ASSERT_EQ(rc, ERR_INVALID_ARGS, mx_status_get_string(rc));
    END_TEST;
}

bool VerifyMissingTree(void) {
    BEGIN_TEST;
    Tree merkleTree;
    InitData(kSmall);
    mx_status_t rc =
        merkleTree.Create(gData, gDataLen, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.Verify(gData, kNodeSize, nullptr, gTreeLen, 0, kNodeSize,
                           gDigest);
    ASSERT_EQ(rc, ERR_INVALID_ARGS, mx_status_get_string(rc));
    END_TEST;
}

bool VerifyUnalignedTreeLength(void) {
    BEGIN_TEST;
    Tree merkleTree;
    auto& data_failures = merkleTree.data_failures();
    auto& tree_failures = merkleTree.tree_failures();
    InitData(kSmall);
    mx_status_t rc =
        merkleTree.Create(gData, gDataLen, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.Verify(gData, gDataLen, gTree, gTreeLen + 1, gOffset,
                           gLength, gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    ASSERT_EQ(data_failures.size(), 0, "Wrong number of data_failures");
    ASSERT_EQ(tree_failures.size(), 0, "Wrong number of tree_failures");
    END_TEST;
}

bool VerifyUnalignedDataLength(void) {
    BEGIN_TEST;
    Tree merkleTree;
    auto& data_failures = merkleTree.data_failures();
    auto& tree_failures = merkleTree.tree_failures();
    InitData(kSmall);
    mx_status_t rc =
        merkleTree.Create(gData, gDataLen, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.Verify(gData, gDataLen - 1, gTree, gTreeLen, gOffset,
                           gLength, gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    ASSERT_EQ(data_failures.size(), 0, "Wrong number of data_failures");
    ASSERT_EQ(tree_failures.size(), 0, "Wrong number of tree_failures");
    END_TEST;
}

bool VerifyTreeTooSmall(void) {
    BEGIN_TEST;
    Tree merkleTree;
    InitData(kSmall);
    mx_status_t rc =
        merkleTree.Create(gData, gDataLen, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    gTreeLen = merkleTree.GetTreeLength(kSmall);
    rc = merkleTree.Verify(gData, gDataLen, gTree, gTreeLen - 1, gOffset,
                           gLength, gDigest);
    ASSERT_EQ(rc, ERR_BUFFER_TOO_SMALL, mx_status_get_string(rc));
    END_TEST;
}

bool VerifyDataUnaligned(void) {
    BEGIN_TEST;
    Tree merkleTree;
    auto& data_failures = merkleTree.data_failures();
    auto& tree_failures = merkleTree.tree_failures();
    InitData(kUnaligned);
    mx_status_t rc =
        merkleTree.Create(gData, gDataLen, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    gTreeLen = merkleTree.GetTreeLength(kUnaligned);
    rc = merkleTree.Verify(gData, gDataLen, gTree, gTreeLen, gOffset,
                           gDataLen - gOffset, gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    ASSERT_EQ(data_failures.size(), 0, "Wrong number of data_failures");
    ASSERT_EQ(tree_failures.size(), 0, "Wrong number of tree_failures");
    END_TEST;
}

bool VerifyUnalignedOffset(void) {
    BEGIN_TEST;
    Tree merkleTree;
    auto& data_failures = merkleTree.data_failures();
    auto& tree_failures = merkleTree.tree_failures();
    InitData(kSmall);
    mx_status_t rc =
        merkleTree.Create(gData, gDataLen, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.Verify(gData, gDataLen, gTree, gTreeLen, gOffset - 1,
                           gLength, gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    ASSERT_EQ(data_failures.size(), 0, "Wrong number of data_failures");
    ASSERT_EQ(tree_failures.size(), 0, "Wrong number of tree_failures");
    END_TEST;
}

bool VerifyUnalignedLength(void) {
    BEGIN_TEST;
    Tree merkleTree;
    auto& data_failures = merkleTree.data_failures();
    auto& tree_failures = merkleTree.tree_failures();
    InitData(kSmall);
    mx_status_t rc =
        merkleTree.Create(gData, gDataLen, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.Verify(gData, gDataLen, gTree, gTreeLen, gOffset,
                           gLength - 1, gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    ASSERT_EQ(data_failures.size(), 0, "Wrong number of data_failures");
    ASSERT_EQ(tree_failures.size(), 0, "Wrong number of tree_failures");
    END_TEST;
}

bool VerifyOutOfBounds(void) {
    BEGIN_TEST;
    Tree merkleTree;
    InitData(kSmall);
    mx_status_t rc =
        merkleTree.Create(gData, gDataLen, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.Verify(gData, gDataLen, gTree, gTreeLen,
                           gDataLen - kNodeSize, gLength, gDigest);
    ASSERT_EQ(rc, ERR_INVALID_ARGS, mx_status_get_string(rc));
    END_TEST;
}

bool VerifyZeroLength(void) {
    BEGIN_TEST;
    Tree merkleTree;
    auto& data_failures = merkleTree.data_failures();
    auto& tree_failures = merkleTree.tree_failures();
    InitData(kSmall);
    mx_status_t rc =
        merkleTree.Create(gData, gDataLen, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    rc = merkleTree.Verify(gData, gDataLen, gTree, gTreeLen, gOffset, 0,
                           gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    ASSERT_EQ(data_failures.size(), 0, "Wrong number of data_failures");
    ASSERT_EQ(tree_failures.size(), 0, "Wrong number of tree_failures");
    END_TEST;
}

bool VerifyBadRoot(void) {
    BEGIN_TEST;
    Tree merkleTree;
    auto& data_failures = merkleTree.data_failures();
    auto& tree_failures = merkleTree.tree_failures();
    InitData(kLarge);
    mx_status_t rc =
        merkleTree.Create(gData, gDataLen, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    uint8_t digest[Digest::kLength];
    rc = gDigest.CopyTo(digest, sizeof(digest));
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    digest[0] ^= 1;
    gDigest = digest;
    rc = merkleTree.Verify(gData, gDataLen, gTree, gTreeLen, gOffset, gLength,
                           gDigest);
    ASSERT_EQ(rc, ERR_IO_DATA_INTEGRITY, mx_status_get_string(rc));
    ASSERT_EQ(data_failures.size(), 0, "Wrong number of data_failures");
    ASSERT_EQ(tree_failures.size(), 1, "Wrong number of tree_failures");
    ASSERT_EQ(tree_failures[0], merkleTree.GetTreeLength(kLarge) - kNodeSize,
              "Wrong offset for tree_failure");
    END_TEST;
}

bool VerifyGoodPartOfBadTree(void) {
    BEGIN_TEST;
    Tree merkleTree;
    auto& data_failures = merkleTree.data_failures();
    auto& tree_failures = merkleTree.tree_failures();
    InitData(kLarge);
    mx_status_t rc =
        merkleTree.Create(gData, gDataLen, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    // gTree[0] ^= 1;
    rc = merkleTree.Verify(gData, gDataLen, gTree, gTreeLen, 256 * kNodeSize,
                           kNodeSize, gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    ASSERT_EQ(data_failures.size(), 0, "Wrong number of data_failures");
    ASSERT_EQ(tree_failures.size(), 0, "Wrong number of tree_failures");
    END_TEST;
}

bool VerifyBadTree(void) {
    BEGIN_TEST;
    Tree merkleTree;
    auto& data_failures = merkleTree.data_failures();
    auto& tree_failures = merkleTree.tree_failures();
    InitData(kLarge);
    mx_status_t rc =
        merkleTree.Create(gData, gDataLen, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    uint64_t hash_off = gOffset / kNodeSize * Digest::kLength;
    gTree[hash_off] ^= 1;
    rc = merkleTree.Verify(gData, gDataLen, gTree, gTreeLen, gOffset, gLength,
                           gDigest);
    ASSERT_EQ(rc, ERR_IO_DATA_INTEGRITY, mx_status_get_string(rc));
    ASSERT_EQ(data_failures.size(), 0, "Wrong number of data_failures");
    ASSERT_EQ(tree_failures.size(), 1, "Wrong number of tree_failures");
    ASSERT_EQ(tree_failures[0], hash_off - (hash_off % kNodeSize),
              "Wrong offset for tree_failure");
    END_TEST;
}

bool VerifyGoodPartOfBadLeaves(void) {
    BEGIN_TEST;
    Tree merkleTree;
    auto& data_failures = merkleTree.data_failures();
    auto& tree_failures = merkleTree.tree_failures();
    InitData(kSmall);
    mx_status_t rc =
        merkleTree.Create(gData, gDataLen, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    gData[0] ^= 1;
    rc = merkleTree.Verify(gData, gDataLen, gTree, gTreeLen, gOffset, gLength,
                           gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    ASSERT_EQ(data_failures.size(), 0, "Wrong number of data_failures");
    ASSERT_EQ(tree_failures.size(), 0, "Wrong number of tree_failures");
    END_TEST;
}

bool VerifyBadLeaves(void) {
    BEGIN_TEST;
    Tree merkleTree;
    auto& data_failures = merkleTree.data_failures();
    auto& tree_failures = merkleTree.tree_failures();
    InitData(kSmall);
    mx_status_t rc =
        merkleTree.Create(gData, gDataLen, gTree, gTreeLen, &gDigest);
    ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
    gData[gOffset] ^= 1;
    rc = merkleTree.Verify(gData, gDataLen, gTree, gTreeLen, gOffset, gLength,
                           gDigest);
    ASSERT_EQ(rc, ERR_IO_DATA_INTEGRITY, mx_status_get_string(rc));
    ASSERT_EQ(data_failures.size(), 1, "Wrong number of data_failures");
    ASSERT_EQ(data_failures[0], gOffset, "Wrong offset for data_failure");
    ASSERT_EQ(tree_failures.size(), 0, "Wrong number of tree_failures");
    END_TEST;
}

bool CreateAndVerifyHugePRNGData(void) {
    BEGIN_TEST;
    Tree merkleTree;
    auto& data_failures = merkleTree.data_failures();
    auto& tree_failures = merkleTree.tree_failures();
    mx_status_t rc = NO_ERROR;
    uint8_t digest[Digest::kLength];
    for (gDataLen = kNodeSize; gDataLen <= sizeof(gData); gDataLen <<= 1) {
        // Generate random data
        for (size_t i = 0; i < gDataLen; ++i) {
            gData[i] = static_cast<uint8_t>(rand());
        }
        // Create the Merkle tree
        gTreeLen = merkleTree.GetTreeLength(gDataLen);
        rc = merkleTree.Create(gData, gDataLen, gTree, gTreeLen, &gDigest);
        ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
        // Randomly pick one of the four cases below.
        rc = gDigest.CopyTo(digest, sizeof(digest));
        ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
        size_t n = (rand() % 16) + 1;
        switch (rand() % 4) {
        case 1:
            // Flip bits in root digest
            for (size_t i = 0; i < n; ++i) {
                uint8_t tmp = static_cast<uint8_t>(rand()) % 8;
                digest[rand() % Digest::kLength] ^=
                    static_cast<uint8_t>(1 << tmp);
            }
            gDigest = digest;
            rc = merkleTree.Verify(gData, gDataLen, gTree, gTreeLen, 0,
                                   gDataLen, gDigest);
            ASSERT_EQ(rc, ERR_IO_DATA_INTEGRITY, mx_status_get_string(rc));
            if (gTreeLen < kNodeSize) {
                ASSERT_EQ(data_failures.size(), 1,
                          "Wrong number of data_failures with bad root");
                ASSERT_EQ(tree_failures.size(), 0,
                          "Wrong number of tree_failures with bad root");
                ASSERT_EQ(data_failures[0], 0, "Not a root digest failure");
            } else {
                ASSERT_EQ(data_failures.size(), 0,
                          "Wrong number of data_failures with bad root");
                ASSERT_EQ(tree_failures.size(), 1,
                          "Wrong number of tree_failures with bad root");
                ASSERT_EQ(tree_failures[0], gTreeLen - kNodeSize,
                          "Not a root digest failure");
            }
            break;
        case 2:
            // Flip bit in data
            for (size_t i = 0; i < n; ++i) {
                uint8_t tmp = static_cast<uint8_t>(rand()) % 8;
                gData[rand() % gDataLen] ^= static_cast<uint8_t>(1 << tmp);
            }
            rc = merkleTree.Verify(gData, gDataLen, gTree, gTreeLen, 0,
                                   gDataLen, gDigest);
            ASSERT_EQ(rc, ERR_IO_DATA_INTEGRITY, mx_status_get_string(rc));
            ASSERT_GT(data_failures.size(), 0,
                      "Wrong number of data_failures with bad data");
            ASSERT_LE(data_failures.size(), n,
                      "Wrong number of data_failures with bad data");
            ASSERT_EQ(tree_failures.size(), 0,
                      "Wrong number of tree_failures with bad data");
            break;
        case 3:
            // Flip bit in tree (if large enough to have a tree)
            for (size_t i = 0; i < n && gTreeLen > 0; ++i) {
                uint8_t tmp = static_cast<uint8_t>(rand()) % 8;
                gTree[rand() % gTreeLen] ^= static_cast<uint8_t>(1 << tmp);
            }
            rc = merkleTree.Verify(gData, gDataLen, gTree, gTreeLen, 0,
                                   gDataLen, gDigest);

            if (gTreeLen < kNodeSize) {
                ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
                ASSERT_EQ(data_failures.size(), 0,
                          "Wrong number of data_failures with bad tree");
                ASSERT_EQ(tree_failures.size(), 0,
                          "Wrong number of tree_failures with bad tree");
            } else {
                ASSERT_EQ(rc, ERR_IO_DATA_INTEGRITY, mx_status_get_string(rc));
                ASSERT_EQ(data_failures.size(), 0,
                          "Wrong number of data_failures with bad tree");
                ASSERT_GT(tree_failures.size(), 0,
                          "Wrong number of tree_failures with bad tree");
                ASSERT_LE(tree_failures.size(), n,
                          "Wrong number of tree_failures with bad tree");
            }
            break;
        default:
            // Normal verification without modification
            rc = merkleTree.Verify(gData, gDataLen, gTree, gTreeLen, 0,
                                   gDataLen, gDigest);
            ASSERT_EQ(rc, NO_ERROR, mx_status_get_string(rc));
            break;
        }
    }
    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(MerkleTreeTests)
RUN_TEST(GetTreeLength)
RUN_TEST(CreateInit)
RUN_TEST(CreateInitMissingTree)
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
RUN_TEST(CreateFinal)
RUN_TEST(CreateFinalMissingInit)
RUN_TEST(CreateFinalWithoutData)
RUN_TEST(CreateFinalWithoutTree)
RUN_TEST(CreateFinalMissingDigest)
RUN_TEST(CreateFinalIncompleteData)
RUN_TEST(Create)
RUN_TEST(CreateCWrappers)
RUN_TEST(CreateByteByByte)
RUN_TEST(CreateWithoutData)
RUN_TEST(CreateWithoutTree)
RUN_TEST(CreateMissingData)
RUN_TEST(CreateMissingTree)
RUN_TEST(CreateTreeTooSmall)
RUN_TEST(CreateDataUnaligned)
RUN_TEST(SetRanges)
RUN_TEST(SetRangesEmpty)
RUN_TEST(SetRangesFull)
RUN_TEST(SetRangesUnalignedOffset)
RUN_TEST(SetRangesUnalignedLength)
RUN_TEST(SetRangesOutOfBounds)
RUN_TEST(Verify)
RUN_TEST(VerifyCWrapper)
RUN_TEST(VerifyNodeByNode)
RUN_TEST(VerifyWithoutData)
RUN_TEST(VerifyWithoutTree)
RUN_TEST(VerifyMissingData)
RUN_TEST(VerifyMissingTree)
RUN_TEST(VerifyUnalignedTreeLength)
RUN_TEST(VerifyUnalignedDataLength)
RUN_TEST(VerifyTreeTooSmall)
RUN_TEST(VerifyDataUnaligned)
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
