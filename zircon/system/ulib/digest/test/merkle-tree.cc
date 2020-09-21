// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <algorithm>

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <digest/node-digest.h>
#include <zxtest/zxtest.h>

namespace digest {
namespace testing {

// The MerkleTree tests below are naturally sensitive to the shape of the Merkle
// tree. These determine those sizes in a consistent way.
const uint64_t kNodeSize = kDefaultNodeSize;
const uint64_t kDigestsPerNode = kNodeSize / kSha256Length;

// The hard-coded trees used for testing were created by using sha256sum on
// files generated using echo -ne, dd, and xxd
struct TreeParam {
  size_t data_len;
  size_t tree_len;
  const char digest[(kSha256Length * 2) + 1];
} kTreeParams[] = {
    {0, 0, "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b"},
    {1, 0, "0967e0f62a104d1595610d272dfab3d2fa2fe07be0eebce13ef5d79db142610e"},
    {kNodeSize / 2, 0, "0a90612c255555469dead72c8fdc41eec06dfe04a30a1f2b7c480ff95d20c5ec"},
    {kNodeSize - 1, 0, "f2abd690381bab3ce485c814d05c310b22c34a7441418b5c1a002c344a80e730"},
    {kNodeSize, 0, "68d131bc271f9c192d4f6dcd8fe61bef90004856da19d0f2f514a7f4098b0737"},
    {kNodeSize + 1, kNodeSize, "374781f7d770b6ee9c1a63e186d2d0ccdad10d6aef4fd027e82b1be5b70a2a0c"},
    {kNodeSize * 8, kNodeSize, "f75f59a944d2433bc6830ec243bfefa457704d2aed12f30539cd4f18bf1d62cf"},
    {kNodeSize * (kDigestsPerNode + 1), kNodeSize * 3,
     "7d75dfb18bfd48e03b5be4e8e9aeea2f89880cb81c1551df855e0d0a0cc59a67"},
    {kNodeSize * (kDigestsPerNode + 1) + (kNodeSize / 2), kNodeSize * 3,
     "7577266aa98ce587922fdc668c186e27f3c742fb1b732737153b70ae46973e43"},
};
const size_t kNumTreeParams = sizeof(kTreeParams) / sizeof(struct TreeParam);

template <class MT>
void TestGetTreeLength() {
  MT mt;
  for (size_t i = 0; i < kNumTreeParams; ++i) {
    EXPECT_OK(mt.SetDataLength(kTreeParams[i].data_len));
    EXPECT_EQ(mt.GetTreeLength(), kTreeParams[i].tree_len);
  }
}
TEST(MerkleTree, GetTreeLength) {
  TestGetTreeLength<MerkleTreeCreator>();
  TestGetTreeLength<MerkleTreeVerifier>();
}

template <class MT>
void TestSetTree() {
  MT mt;
  uint8_t tree[kNodeSize * 3];
  uint8_t root[kSha256Length];
  for (size_t i = 0; i < kNumTreeParams; ++i) {
    size_t tree_len = kTreeParams[i].tree_len;
    ASSERT_OK(mt.SetDataLength(kTreeParams[i].data_len));
    if (tree_len > 0) {
      EXPECT_STATUS(mt.SetTree(nullptr, tree_len, root, sizeof(root)), ZX_ERR_INVALID_ARGS);
      EXPECT_STATUS(mt.SetTree(tree, tree_len - 1, root, sizeof(root)), ZX_ERR_BUFFER_TOO_SMALL);
    }
    EXPECT_STATUS(mt.SetTree(tree, tree_len, nullptr, sizeof(root)), ZX_ERR_INVALID_ARGS);
    EXPECT_STATUS(mt.SetTree(tree, tree_len, root, sizeof(root) - 1), ZX_ERR_BUFFER_TOO_SMALL);
    EXPECT_OK(mt.SetTree(tree, tree_len, root, sizeof(root)));
  }
}
TEST(MerkleTree, SetTree) {
  TestSetTree<MerkleTreeCreator>();
  TestSetTree<MerkleTreeVerifier>();
}

void MaxDataAndTree(std::unique_ptr<uint8_t[]> *out_data, std::unique_ptr<uint8_t[]> *out_tree) {
  size_t data_len = 0;
  size_t tree_len = 0;
  for (size_t i = 0; i < kNumTreeParams; ++i) {
    data_len = std::max(data_len, kTreeParams[i].data_len);
    tree_len = std::max(tree_len, kTreeParams[i].tree_len);
  }
  fbl::AllocChecker ac;
  ASSERT_NE(data_len, 0);
  std::unique_ptr<uint8_t[]> data(new (&ac) uint8_t[data_len]);
  ASSERT_TRUE(ac.check());
  ASSERT_NE(tree_len, 0);
  std::unique_ptr<uint8_t[]> tree(new (&ac) uint8_t[tree_len]);
  ASSERT_TRUE(ac.check());
  memset(data.get(), 0xff, data_len);
  memset(tree.get(), 0x00, tree_len);
  *out_data = std::move(data);
  *out_tree = std::move(tree);
}

TEST(MerkleTree, Create) {
  MerkleTreeCreator creator;
  std::unique_ptr<uint8_t[]> data;
  std::unique_ptr<uint8_t[]> tree;
  ASSERT_NO_FATAL_FAILURES(MaxDataAndTree(&data, &tree));
  uint8_t root[kSha256Length];
  Digest digest;
  for (size_t i = 0; i < kNumTreeParams; ++i) {
    size_t data_len = kTreeParams[i].data_len;
    size_t tree_len = kTreeParams[i].tree_len;
    ASSERT_OK(digest.Parse(kTreeParams[i].digest));
    // Valid, added all at once
    memset(root, 0, sizeof(root));
    ASSERT_OK(creator.SetDataLength(data_len));
    ASSERT_OK(creator.SetTree(tree.get(), tree_len, root, sizeof(root)));
    EXPECT_OK(creator.Append(data.get(), data_len));
    EXPECT_BYTES_EQ(root, digest.get(), sizeof(root));
    // Can reuse creator
    memset(root, 0, sizeof(root));
    ASSERT_OK(creator.SetDataLength(data_len));
    ASSERT_OK(creator.SetTree(tree.get(), tree_len, root, sizeof(root)));
    // Adding zero length has no effect
    EXPECT_OK(creator.Append(nullptr, 0));
    if (data_len != 0) {
      EXPECT_BYTES_NE(root, digest.get(), sizeof(root));
      // Not enough data
      for (size_t j = 0; j < data_len - 1; ++j) {
        EXPECT_OK(creator.Append(&data[j], 1));
      }
      // Valid, added byte by byte
      EXPECT_OK(creator.Append(&data[data_len - 1], 1));
    }
    EXPECT_BYTES_EQ(root, digest.get(), sizeof(root));
    // Adding zero length has no effect
    EXPECT_OK(creator.Append(nullptr, 0));
    EXPECT_BYTES_EQ(root, digest.get(), sizeof(root));
    // Too much
    EXPECT_STATUS(creator.Append(data.get(), 1), ZX_ERR_INVALID_ARGS);
    // Static
    std::unique_ptr<uint8_t[]> tmp_tree;
    EXPECT_OK(MerkleTreeCreator::Create(data.get(), data_len, &tmp_tree, &tree_len, &digest));
    EXPECT_EQ(tree_len, kTreeParams[i].tree_len);
    EXPECT_BYTES_EQ(digest.get(), root, sizeof(root));
  }
}

TEST(MerkleTree, Verify) {
  srand(zxtest::Runner::GetInstance()->random_seed());
  MerkleTreeCreator creator;
  MerkleTreeVerifier verifier;
  std::unique_ptr<uint8_t[]> data;
  std::unique_ptr<uint8_t[]> tree;
  ASSERT_NO_FATAL_FAILURES(MaxDataAndTree(&data, &tree));
  uint8_t root[kSha256Length];
  Digest digest;
  size_t flip;
  for (size_t i = 0; i < kNumTreeParams; ++i) {
    size_t data_len = kTreeParams[i].data_len;
    size_t tree_len = kTreeParams[i].tree_len;
    ASSERT_OK(digest.Parse(kTreeParams[i].digest));
    ASSERT_OK(creator.SetDataLength(data_len));
    ASSERT_OK(creator.SetTree(tree.get(), tree_len, root, sizeof(root)));
    ASSERT_OK(creator.Append(data.get(), data_len));
    // Verify all
    EXPECT_OK(verifier.SetDataLength(data_len));
    EXPECT_OK(verifier.SetTree(tree.get(), tree_len, root, sizeof(root)));
    EXPECT_OK(verifier.Verify(data.get(), data_len, 0));
    // Empty range is trivially true
    EXPECT_OK(verifier.Verify(nullptr, 0, 0));
    // Flip a byte in the root
    flip = rand() % sizeof(root);
    root[flip] ^= 0xff;
    EXPECT_STATUS(verifier.Verify(data.get(), data_len, 0), ZX_ERR_IO_DATA_INTEGRITY);
    root[flip] ^= 0xff;
    // Flip a byte in the tree
    if (tree_len > 0) {
      flip = rand() % tree_len;
      tree[flip] ^= 0xff;
      EXPECT_STATUS(verifier.Verify(data.get(), data_len, 0), ZX_ERR_IO_DATA_INTEGRITY);
      tree[flip] ^= 0xff;
    }

    for (size_t data_off = 0; data_off < data_len; data_off += kNodeSize) {
      // Unaligned ( +2 doesn't line up with any node boundarys or data ends in kTreeParams)
      uint8_t *buf = &data[data_off];
      size_t buf_len = std::min(data_len - data_off, kNodeSize);
      EXPECT_STATUS(verifier.Verify(buf, buf_len + 2, data_off), ZX_ERR_INVALID_ARGS);
      // Verify each node
      EXPECT_OK(verifier.Verify(buf, buf_len, data_off));
      // Flip a byte in the root
      flip = rand() % sizeof(root);
      root[flip] ^= 0xff;
      EXPECT_STATUS(verifier.Verify(buf, buf_len, data_off), ZX_ERR_IO_DATA_INTEGRITY);
      root[flip] ^= 0xff;
    }
    // Flip a byte in data; (statically) verify only that node fails
    if (data_len != 0) {
      flip = rand() % data_len;
      data[flip] ^= 0xff;
      size_t data_off = flip;
      size_t buf_len = 1;
      EXPECT_OK(verifier.Align(&data_off, &buf_len));
      uint8_t *buf = &data[data_off];
      const uint8_t *after = buf + buf_len;
      size_t after_off = data_off + buf_len;
      size_t after_len = data_len - after_off;
      EXPECT_OK(MerkleTreeVerifier::Verify(data.get(), data_off, 0, data_len, tree.get(), tree_len,
                                           digest));
      EXPECT_STATUS(MerkleTreeVerifier::Verify(buf, buf_len, data_off, data_len, tree.get(),
                                               tree_len, digest),
                    ZX_ERR_IO_DATA_INTEGRITY);
      EXPECT_OK(MerkleTreeVerifier::Verify(after, after_len, after_off, data_len, tree.get(),
                                           tree_len, digest));
      data[flip] ^= 0xff;
    }
  }
}

TEST(MerkleTree, CalculateMerkleTreeSize) {
  for (const auto &tree_params : kTreeParams) {
    EXPECT_EQ(tree_params.tree_len, CalculateMerkleTreeSize(tree_params.data_len, kDefaultNodeSize),
              "Data len: %lu, digest: %s", tree_params.data_len, tree_params.digest);
  }
}

}  // namespace testing
}  // namespace digest
