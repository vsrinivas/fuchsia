// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <zxtest/zxtest.h>

namespace {

////////////////
// Test support.

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
struct TreeParam {
  size_t data_len;
  size_t tree_len;
  const char digest[(Digest::kLength * 2) + 1];
} kTreeParams[] = {
    {0, 0, "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b"},
    {1, 0, "0967e0f62a104d1595610d272dfab3d2fa2fe07be0eebce13ef5d79db142610e"},
    {kNodeSize / 2, 0, "0a90612c255555469dead72c8fdc41eec06dfe04a30a1f2b7c480ff95d20c5ec"},
    {kNodeSize - 1, 0, "f2abd690381bab3ce485c814d05c310b22c34a7441418b5c1a002c344a80e730"},
    {kNodeSize, 0, "68d131bc271f9c192d4f6dcd8fe61bef90004856da19d0f2f514a7f4098b0737"},
    {kNodeSize + 1, kNodeSize, "374781f7d770b6ee9c1a63e186d2d0ccdad10d6aef4fd027e82b1be5b70a2a0c"},
    {kSmall, kNodeSize, "f75f59a944d2433bc6830ec243bfefa457704d2aed12f30539cd4f18bf1d62cf"},
    {kLarge, kNodeSize * 3, "7d75dfb18bfd48e03b5be4e8e9aeea2f89880cb81c1551df855e0d0a0cc59a67"},
    {kUnalignedLarge, kNodeSize * 3,
     "7577266aa98ce587922fdc668c186e27f3c742fb1b732737153b70ae46973e43"},
};
const size_t kNumTreeParams = sizeof(kTreeParams) / sizeof(struct TreeParam);

// These tests use a text fixture to reduce the amount of repetitive setup.
class MerkleTreeTestCase : public zxtest::Test {
 protected:
  void SetUp() override {
    memset(data_, 0xff, sizeof(data_));
    index_ = 0;
  }

  zx_status_t InitCreate(size_t data_len) {
    size_t tree_len = MerkleTree::GetTreeLength(data_len);
    if (data_len > sizeof(data_) || tree_len > sizeof(tree_)) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    data_len_ = data_len;
    tree_len_ = tree_len;
    return ZX_OK;
  }

  zx_status_t InitVerify(size_t data_len) {
    zx_status_t rc;
    if ((rc = InitCreate(data_len)) != ZX_OK ||
        (rc = MerkleTree::Create(data_, data_len_, tree_, tree_len_, &actual_)) != ZX_OK) {
      return rc;
    }
    return ZX_OK;
  }

  zx_status_t NextCreate() {
    zx_status_t rc;
    if (index_ >= kNumTreeParams) {
      return ZX_ERR_STOP;
    }
    if ((rc = InitCreate(kTreeParams[index_].data_len)) != ZX_OK ||
        (rc = expected_.Parse(kTreeParams[index_].digest, strlen(kTreeParams[index_].digest))) !=
            ZX_OK) {
      return rc;
    }
    index_++;
    return ZX_ERR_NEXT;
  }

  zx_status_t NextVerify() {
    zx_status_t rc;
    if ((rc = NextCreate()) != ZX_ERR_NEXT ||
        (rc = MerkleTree::Create(data_, data_len_, tree_, tree_len_, &actual_)) != ZX_OK) {
      return rc;
    }
    return ZX_ERR_NEXT;
  }

  size_t index_;

  uint8_t data_[kUnalignedLarge];
  uint8_t tree_[kNodeSize * 3];
  size_t data_len_;
  size_t tree_len_;
  MerkleTree mt_;
  Digest actual_;
  Digest expected_;
};

////////////////
// Test cases

TEST_F(MerkleTreeTestCase, GetTreeLength) {
  for (auto rc = NextCreate(); rc != ZX_ERR_STOP; rc = NextCreate()) {
    ASSERT_STATUS(rc, ZX_ERR_NEXT);
    EXPECT_EQ(tree_len_, MerkleTree::GetTreeLength(data_len_));
  }
}

TEST_F(MerkleTreeTestCase, GetTreeLengthC) {
  for (auto rc = NextCreate(); rc != ZX_ERR_STOP; rc = NextCreate()) {
    ASSERT_STATUS(rc, ZX_ERR_NEXT);
    EXPECT_EQ(tree_len_, merkle_tree_get_tree_length(data_len_));
  }
}

TEST_F(MerkleTreeTestCase, CreateInit) {
  ASSERT_OK(InitCreate(kLarge));
  EXPECT_OK(mt_.CreateInit(data_len_, tree_len_));
}

TEST_F(MerkleTreeTestCase, CreateInitWithoutData) {
  EXPECT_OK(mt_.CreateInit(0, MerkleTree::GetTreeLength(kLarge)));
  EXPECT_OK(mt_.CreateInit(0, 0));
}

TEST_F(MerkleTreeTestCase, CreateInitWithoutTree) { EXPECT_OK(mt_.CreateInit(kNodeSize, 0)); }

TEST_F(MerkleTreeTestCase, CreateInitTreeTooSmall) {
  ASSERT_OK(InitCreate(kLarge));
  EXPECT_STATUS(mt_.CreateInit(kLarge, tree_len_ - 1), ZX_ERR_BUFFER_TOO_SMALL);
}

TEST_F(MerkleTreeTestCase, CreateUpdate) {
  ASSERT_OK(InitCreate(kLarge));
  EXPECT_OK(mt_.CreateInit(data_len_, tree_len_));
  EXPECT_OK(mt_.CreateUpdate(data_, data_len_, tree_));
}

TEST_F(MerkleTreeTestCase, CreateUpdateMissingInit) {
  ASSERT_OK(InitCreate(kLarge));
  EXPECT_STATUS(mt_.CreateUpdate(data_, data_len_, tree_), ZX_ERR_BAD_STATE);
}

TEST_F(MerkleTreeTestCase, CreateUpdateMissingData) {
  ASSERT_OK(InitCreate(kLarge));
  EXPECT_OK(mt_.CreateInit(data_len_, tree_len_));
  EXPECT_STATUS(mt_.CreateUpdate(nullptr, data_len_, tree_), ZX_ERR_INVALID_ARGS);
}

TEST_F(MerkleTreeTestCase, CreateUpdateMissingTree) {
  ASSERT_OK(InitCreate(kLarge));
  EXPECT_OK(mt_.CreateInit(data_len_, tree_len_));
  EXPECT_STATUS(mt_.CreateUpdate(data_, data_len_, nullptr), ZX_ERR_INVALID_ARGS);
}

TEST_F(MerkleTreeTestCase, CreateUpdateWithoutData) {
  ASSERT_OK(InitCreate(kLarge));
  EXPECT_OK(mt_.CreateInit(data_len_, tree_len_));
  EXPECT_OK(mt_.CreateUpdate(data_, 0, tree_));
  EXPECT_OK(mt_.CreateUpdate(nullptr, 0, tree_));
}

TEST_F(MerkleTreeTestCase, CreateUpdateWithoutTree) {
  ASSERT_OK(InitCreate(kNodeSize));
  EXPECT_OK(mt_.CreateInit(data_len_, 0));
  EXPECT_OK(mt_.CreateUpdate(data_, data_len_, nullptr));
}

TEST_F(MerkleTreeTestCase, CreateUpdateTooMuchData) {
  ASSERT_OK(InitCreate(kLarge));
  EXPECT_OK(mt_.CreateInit(data_len_, tree_len_));
  EXPECT_STATUS(mt_.CreateUpdate(data_, data_len_ + 1, tree_), ZX_ERR_OUT_OF_RANGE);
}

TEST_F(MerkleTreeTestCase, CreateFinalMissingInit) {
  EXPECT_STATUS(mt_.CreateFinal(tree_, &actual_), ZX_ERR_BAD_STATE);
}

TEST_F(MerkleTreeTestCase, CreateFinal) {
  size_t no_data = 0;
  size_t no_tree = 0;
  for (auto rc = NextCreate(); rc != ZX_ERR_STOP; rc = NextCreate()) {
    ASSERT_STATUS(rc, ZX_ERR_NEXT);
    if (data_len_ == 0) {
      no_data++;
    } else if (data_len_ < kNodeSize) {
      no_tree++;
    }
    EXPECT_OK(mt_.CreateInit(data_len_, tree_len_));
    EXPECT_OK(mt_.CreateUpdate(data_, data_len_, tree_));
    EXPECT_OK(mt_.CreateFinal(tree_, &actual_));
    EXPECT_BYTES_EQ(actual_.get(), expected_.get(), Digest::kLength);
  }
  EXPECT_NE(no_data, 0);
  EXPECT_NE(no_tree, 0);
}

TEST_F(MerkleTreeTestCase, CreateFinalMissingDigest) {
  ASSERT_OK(InitCreate(kLarge));
  EXPECT_OK(mt_.CreateInit(data_len_, tree_len_));
  EXPECT_OK(mt_.CreateUpdate(data_, data_len_, tree_));
  EXPECT_STATUS(mt_.CreateFinal(tree_, nullptr), ZX_ERR_INVALID_ARGS);
}

TEST_F(MerkleTreeTestCase, CreateFinalIncompleteData) {
  ASSERT_OK(InitCreate(kLarge));
  EXPECT_OK(mt_.CreateInit(data_len_, tree_len_));
  EXPECT_OK(mt_.CreateUpdate(data_, kLarge - 1, tree_));
  EXPECT_STATUS(mt_.CreateFinal(tree_, &actual_), ZX_ERR_BAD_STATE);
}

TEST_F(MerkleTreeTestCase, Create) {
  for (auto rc = NextCreate(); rc != ZX_ERR_STOP; rc = NextCreate()) {
    ASSERT_STATUS(rc, ZX_ERR_NEXT);
    EXPECT_OK(MerkleTree::Create(data_, data_len_, tree_, tree_len_, &actual_));
    EXPECT_BYTES_EQ(actual_.get(), expected_.get(), Digest::kLength);
  }
}

TEST_F(MerkleTreeTestCase, CreateFinalCAll) {
  for (auto rc = NextCreate(); rc != ZX_ERR_STOP; rc = NextCreate()) {
    ASSERT_STATUS(rc, ZX_ERR_NEXT);
    // Init
    merkle_tree_t* mt = nullptr;
    EXPECT_OK(merkle_tree_create_init(data_len_, tree_len_, &mt));
    // Update
    size_t i = 0;
    while (i + kNodeSize < data_len_) {
      EXPECT_OK(merkle_tree_create_update(mt, data_ + i, kNodeSize, tree_));
      i += kNodeSize;
    }
    EXPECT_OK(merkle_tree_create_update(mt, data_ + i, data_len_ - i, tree_));
    // Final
    uint8_t actual[Digest::kLength];
    EXPECT_OK(merkle_tree_create_final(mt, tree_, &actual, sizeof(actual)));
    EXPECT_BYTES_EQ(expected_.get(), actual, Digest::kLength);
  }
}

TEST_F(MerkleTreeTestCase, CreateC) {
  for (auto rc = NextCreate(); rc != ZX_ERR_STOP; rc = NextCreate()) {
    ASSERT_STATUS(rc, ZX_ERR_NEXT);
    uint8_t actual[Digest::kLength];
    EXPECT_OK(merkle_tree_create(data_, data_len_, tree_, tree_len_, &actual, sizeof(actual)));
    EXPECT_BYTES_EQ(expected_.get(), actual, Digest::kLength);
  }
}

TEST_F(MerkleTreeTestCase, CreateByteByByte) {
  ASSERT_OK(InitCreate(kSmall));
  EXPECT_OK(mt_.CreateInit(data_len_, tree_len_));
  for (uint64_t i = 0; i < data_len_; ++i) {
    EXPECT_OK(mt_.CreateUpdate(data_ + i, 1, tree_));
  }
  EXPECT_OK(mt_.CreateFinal(tree_, &actual_));
  EXPECT_OK(MerkleTree::Create(data_, data_len_, tree_, tree_len_, &expected_));
  EXPECT_BYTES_EQ(actual_.get(), expected_.get(), Digest::kLength);
}

TEST_F(MerkleTreeTestCase, CreateMissingData) {
  ASSERT_OK(InitCreate(kSmall));
  EXPECT_STATUS(MerkleTree::Create(nullptr, data_len_, tree_, tree_len_, &actual_),
                ZX_ERR_INVALID_ARGS);
}

TEST_F(MerkleTreeTestCase, CreateMissingTree) {
  ASSERT_OK(InitCreate(kSmall));
  EXPECT_STATUS(MerkleTree::Create(data_, data_len_, nullptr, kNodeSize, &actual_),
                ZX_ERR_INVALID_ARGS);
}

TEST_F(MerkleTreeTestCase, CreateTreeTooSmall) {
  ASSERT_OK(InitCreate(kSmall));
  EXPECT_STATUS(MerkleTree::Create(data_, data_len_, nullptr, 0, &actual_),
                ZX_ERR_BUFFER_TOO_SMALL);

  // The maximum amount of data representable by a one-node tree.
  size_t max_data_one_node = kNodeSize * (kNodeSize / Digest::kLength);
  EXPECT_STATUS(MerkleTree::Create(data_, max_data_one_node + 1, tree_, kNodeSize, &actual_),
                ZX_ERR_BUFFER_TOO_SMALL);
}

TEST_F(MerkleTreeTestCase, Verify) {
  for (auto rc = NextVerify(); rc != ZX_ERR_STOP; rc = NextVerify()) {
    ASSERT_STATUS(rc, ZX_ERR_NEXT);
    EXPECT_OK(MerkleTree::Verify(data_, data_len_, tree_, tree_len_, 0, data_len_, expected_));
  }
}

TEST_F(MerkleTreeTestCase, VerifyC) {
  for (auto rc = NextVerify(); rc != ZX_ERR_STOP; rc = NextVerify()) {
    ASSERT_STATUS(rc, ZX_ERR_NEXT);
    EXPECT_OK(merkle_tree_verify(data_, data_len_, tree_, tree_len_, 0, data_len_, expected_.get(),
                                 Digest::kLength));
  }
}

TEST_F(MerkleTreeTestCase, VerifyNodeByNode) {
  ASSERT_OK(InitVerify(kSmall));
  for (uint64_t i = 0; i < data_len_; i += kNodeSize) {
    EXPECT_OK(MerkleTree::Verify(data_, data_len_, tree_, tree_len_, i, kNodeSize, actual_));
  }
}

TEST_F(MerkleTreeTestCase, VerifyMissingData) {
  ASSERT_OK(InitVerify(kSmall));
  EXPECT_STATUS(MerkleTree::Verify(nullptr, data_len_, tree_, tree_len_, 0, data_len_, actual_),
                ZX_ERR_INVALID_ARGS);
}

TEST_F(MerkleTreeTestCase, VerifyMissingTree) {
  ASSERT_OK(InitVerify(kLarge));
  EXPECT_STATUS(MerkleTree::Verify(data_, data_len_, nullptr, tree_len_, 0, data_len_, actual_),
                ZX_ERR_INVALID_ARGS);
}

TEST_F(MerkleTreeTestCase, VerifyUnalignedTreeLength) {
  ASSERT_OK(InitVerify(kSmall));
  EXPECT_OK(MerkleTree::Verify(data_, data_len_, tree_, tree_len_ + 1, 0, data_len_, actual_));
}

TEST_F(MerkleTreeTestCase, VerifyUnalignedDataLength) {
  ASSERT_OK(InitVerify(kSmall - 1));
  EXPECT_OK(MerkleTree::Verify(data_, data_len_, tree_, tree_len_, 0, data_len_, actual_));
}

TEST_F(MerkleTreeTestCase, VerifyTreeTooSmall) {
  ASSERT_OK(InitVerify(kSmall));
  EXPECT_STATUS(MerkleTree::Verify(data_, data_len_, tree_, tree_len_ - 1, 0, data_len_, actual_),
                ZX_ERR_BUFFER_TOO_SMALL);
}

TEST_F(MerkleTreeTestCase, VerifyUnalignedOffset) {
  ASSERT_OK(InitVerify(kSmall));
  EXPECT_OK(
      MerkleTree::Verify(data_, data_len_, tree_, tree_len_, kNodeSize - 1, kNodeSize, actual_));
}

TEST_F(MerkleTreeTestCase, VerifyUnalignedLength) {
  ASSERT_OK(InitVerify(kSmall));
  EXPECT_OK(MerkleTree::Verify(data_, data_len_, tree_, tree_len_, 0, data_len_ - 1, actual_));
}

TEST_F(MerkleTreeTestCase, VerifyOutOfBounds) {
  ASSERT_OK(InitVerify(kSmall));
  EXPECT_STATUS(MerkleTree::Verify(data_, data_len_, tree_, tree_len_, data_len_ - kNodeSize,
                                   kNodeSize * 2, actual_),
                ZX_ERR_OUT_OF_RANGE);
}

TEST_F(MerkleTreeTestCase, VerifyZeroLength) {
  ASSERT_OK(InitVerify(kSmall));
  EXPECT_OK(MerkleTree::Verify(data_, data_len_, tree_, tree_len_, 0, 0, actual_));
}

TEST_F(MerkleTreeTestCase, VerifyBadRoot) {
  ASSERT_OK(InitVerify(kLarge));
  // Modify digest
  char str[(Digest::kLength * 2) + 1];
  EXPECT_OK(actual_.ToString(str, sizeof(str)));
  str[0] = (str[0] == '0' ? '1' : '0');
  EXPECT_OK(actual_.Parse(str, strlen(str)));
  // Verify
  EXPECT_STATUS(MerkleTree::Verify(data_, data_len_, tree_, tree_len_, 0, data_len_, actual_),
                ZX_ERR_IO_DATA_INTEGRITY);
}

TEST_F(MerkleTreeTestCase, VerifyGoodPartOfBadTree) {
  ASSERT_OK(InitVerify(kLarge));
  tree_[0] ^= 1;
  EXPECT_OK(MerkleTree::Verify(data_, data_len_, tree_, tree_len_, data_len_ - kNodeSize, kNodeSize,
                               actual_));
}

TEST_F(MerkleTreeTestCase, VerifyBadTree) {
  ASSERT_OK(InitVerify(kLarge));
  tree_[0] ^= 1;
  EXPECT_STATUS(MerkleTree::Verify(data_, data_len_, tree_, tree_len_, 0, 1, actual_),
                ZX_ERR_IO_DATA_INTEGRITY);
}

TEST_F(MerkleTreeTestCase, VerifyGoodPartOfBadLeaves) {
  ASSERT_OK(InitVerify(kSmall));
  data_[0] ^= 1;
  EXPECT_OK(MerkleTree::Verify(data_, data_len_, tree_, tree_len_, kNodeSize, data_len_ - kNodeSize,
                               actual_));
}

TEST_F(MerkleTreeTestCase, VerifyBadLeaves) {
  ASSERT_OK(InitVerify(kSmall));
  data_[0] ^= 1;
  EXPECT_STATUS(MerkleTree::Verify(data_, data_len_, tree_, tree_len_, 0, data_len_, actual_),
                ZX_ERR_IO_DATA_INTEGRITY);
}

TEST_F(MerkleTreeTestCase, CreateAndVerifyHugePRNGData) {
  uint8_t buffer[Digest::kLength];
  for (size_t data_len = kNodeSize; data_len <= sizeof(data_); data_len <<= 1) {
    ASSERT_OK(InitCreate(data_len));
    // Generate random data
    for (uint64_t i = 0; i < data_len; ++i) {
      data_[i] = static_cast<uint8_t>(rand());
    }
    ASSERT_OK(MerkleTree::Create(data_, data_len_, tree_, tree_len_, &actual_));
    // Randomly pick one of the four cases below.
    uint8_t flip = static_cast<uint8_t>(1 << (rand() % 8));

    switch (rand() % 4) {
      case 1: {
        EXPECT_OK(actual_.CopyTo(buffer, sizeof(buffer)));
        // Flip a bit in root digest
        buffer[rand() % Digest::kLength] ^= flip;
        actual_ = buffer;
        EXPECT_STATUS(MerkleTree::Verify(data_, data_len_, tree_, tree_len_, 0, data_len_, actual_),
                      ZX_ERR_IO_DATA_INTEGRITY);
        break;
      }
      case 2: {
        // Flip a bit in data
        data_[rand() % data_len_] ^= flip;
        EXPECT_STATUS(MerkleTree::Verify(data_, data_len_, tree_, tree_len_, 0, data_len_, actual_),
                      ZX_ERR_IO_DATA_INTEGRITY);
        break;
      }
      case 3: {
        // Flip a bit in tree (if large enough to have a tree)
        if (tree_len_ != 0) {
          tree_[rand() % (data_len_ / Digest::kLength)] ^= flip;
          EXPECT_STATUS(
              MerkleTree::Verify(data_, data_len_, tree_, tree_len_, 0, data_len_, actual_),
              ZX_ERR_IO_DATA_INTEGRITY);
        }
        break;
      }
      default:
        // Normal verification without modification
        EXPECT_OK(MerkleTree::Verify(data_, data_len_, tree_, tree_len_, 0, data_len_, actual_));
        break;
    }
  }
}

}  // namespace
