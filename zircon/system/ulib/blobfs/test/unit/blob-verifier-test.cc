// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blob-verifier.h"

#include <random>

#include <zxtest/zxtest.h>

#include "utils.h"

namespace blobfs {
namespace {

class BlobVerifierTest : public zxtest::Test {
 public:
  BlobfsMetrics* Metrics() { return &metrics_; }

  void SetUp() override { srand(zxtest::Runner::GetInstance()->random_seed()); }

 private:
  BlobfsMetrics metrics_;
};

void GenerateTree(const uint8_t* data, size_t len, Digest* out_digest,
                  fbl::Array<uint8_t>* out_tree) {
  digest::MerkleTreeCreator mtc;
  ASSERT_OK(mtc.SetDataLength(len));
  size_t merkle_size = mtc.GetTreeLength();
  fbl::Array<uint8_t> merkle_buf(new uint8_t[merkle_size], merkle_size);
  uint8_t root[digest::kSha256Length];
  ASSERT_OK(mtc.SetTree(merkle_buf.get(), merkle_size, root, sizeof(root)));
  ASSERT_OK(mtc.Append(data, len));
  *out_digest = Digest(root);
  *out_tree = std::move(merkle_buf);
}

void FillWithRandom(uint8_t* buf, size_t len) {
  for (unsigned i = 0; i < len; ++i) {
    buf[i] = (uint8_t)rand();
  }
}

TEST_F(BlobVerifierTest, CreateAndVerify_NullBlob) {
  fbl::Array<uint8_t> unused_merkle_buf;
  Digest digest;
  GenerateTree(nullptr, 0, &digest, &unused_merkle_buf);

  std::unique_ptr<BlobVerifier> verifier;
  ASSERT_OK(BlobVerifier::CreateWithoutTree(std::move(digest), Metrics(), 0ul, nullptr, &verifier));
  EXPECT_OK(verifier->Verify(nullptr, 0ul, 0ul));
  EXPECT_OK(verifier->VerifyPartial(nullptr, 0ul, 0ul, 0ul));
}

TEST_F(BlobVerifierTest, CreateAndVerify_SmallBlob) {
  uint8_t buf[8192];
  FillWithRandom(buf, sizeof(buf));

  fbl::Array<uint8_t> unused_merkle_buf;
  Digest digest;
  GenerateTree(buf, sizeof(buf), &digest, &unused_merkle_buf);

  std::unique_ptr<BlobVerifier> verifier;
  ASSERT_OK(BlobVerifier::CreateWithoutTree(std::move(digest), Metrics(), sizeof(buf), nullptr,
                                            &verifier));

  EXPECT_OK(verifier->Verify(buf, sizeof(buf), sizeof(buf)));

  EXPECT_OK(verifier->VerifyPartial(buf, 8192, 0, 8192));

  // Partial ranges
  EXPECT_EQ(verifier->VerifyPartial(buf, 8191, 0, 8191), ZX_ERR_INVALID_ARGS);

  // Verify past the end
  EXPECT_EQ(verifier->VerifyPartial(buf, 2 * 8192, 0, 2 * 8192), ZX_ERR_INVALID_ARGS);
}

TEST_F(BlobVerifierTest, CreateAndVerify_SmallBlob_DataCorrupted) {
  uint8_t buf[8192];
  FillWithRandom(buf, sizeof(buf));

  fbl::Array<uint8_t> unused_merkle_buf;
  Digest digest;
  GenerateTree(buf, sizeof(buf), &digest, &unused_merkle_buf);

  // Invert one character
  buf[42] = ~(buf[42]);

  std::unique_ptr<BlobVerifier> verifier;
  ASSERT_OK(BlobVerifier::CreateWithoutTree(std::move(digest), Metrics(), sizeof(buf), nullptr,
                                            &verifier));

  EXPECT_EQ(verifier->Verify(buf, sizeof(buf), sizeof(buf)), ZX_ERR_IO_DATA_INTEGRITY);
  EXPECT_EQ(verifier->VerifyPartial(buf, 8192, 0, 8192), ZX_ERR_IO_DATA_INTEGRITY);
}

TEST_F(BlobVerifierTest, CreateAndVerify_BigBlob) {
  size_t sz = 1 << 16;
  fbl::Array<uint8_t> buf(new uint8_t[sz], sz);
  FillWithRandom(buf.get(), sz);

  fbl::Array<uint8_t> merkle_buf;
  Digest digest;
  GenerateTree(buf.get(), sz, &digest, &merkle_buf);

  std::unique_ptr<BlobVerifier> verifier;
  ASSERT_OK(BlobVerifier::Create(std::move(digest), Metrics(), merkle_buf.get(), merkle_buf.size(),
                                 sz, nullptr, &verifier));

  EXPECT_OK(verifier->Verify(buf.get(), sz, sz));

  EXPECT_OK(verifier->VerifyPartial(buf.get(), sz, 0, sz));

  // Block-by-block
  for (size_t i = 0; i < sz; i += 8192) {
    EXPECT_OK(verifier->VerifyPartial(buf.get() + i, 8192, i, 8192));
  }

  // Partial ranges
  EXPECT_EQ(verifier->VerifyPartial(buf.data(), 8191, 0, 8191), ZX_ERR_INVALID_ARGS);

  // Verify past the end
  EXPECT_EQ(verifier->VerifyPartial(buf.data() + (sz - 8192), 2 * 8192, sz - 8192, 2 * 8192),
            ZX_ERR_INVALID_ARGS);
}

TEST_F(BlobVerifierTest, CreateAndVerify_BigBlob_DataCorrupted) {
  size_t sz = 1 << 16;
  fbl::Array<uint8_t> buf(new uint8_t[sz], sz);
  FillWithRandom(buf.get(), sz);

  fbl::Array<uint8_t> merkle_buf;
  Digest digest;
  GenerateTree(buf.get(), sz, &digest, &merkle_buf);

  // Invert a char in the first block. All other blocks are still valid.
  buf.get()[42] = ~(buf.get()[42]);

  std::unique_ptr<BlobVerifier> verifier;
  ASSERT_OK(BlobVerifier::Create(std::move(digest), Metrics(), merkle_buf.get(), merkle_buf.size(),
                                 sz, nullptr, &verifier));

  EXPECT_EQ(verifier->Verify(buf.get(), sz, sz), ZX_ERR_IO_DATA_INTEGRITY);

  EXPECT_EQ(verifier->VerifyPartial(buf.get(), sz, 0, sz), ZX_ERR_IO_DATA_INTEGRITY);

  // Block-by-block -- first block fails, rest succeed
  for (size_t i = 0; i < sz; i += 8192) {
    zx_status_t expected_status = i == 0 ? ZX_ERR_IO_DATA_INTEGRITY : ZX_OK;
    EXPECT_EQ(verifier->VerifyPartial(buf.get() + i, 8192, i, 8192), expected_status);
  }
}

TEST_F(BlobVerifierTest, CreateAndVerify_BigBlob_MerkleCorrupted) {
  size_t sz = 1 << 16;
  fbl::Array<uint8_t> buf(new uint8_t[sz], sz);
  FillWithRandom(buf.get(), sz);

  fbl::Array<uint8_t> merkle_buf;
  Digest digest;
  GenerateTree(buf.get(), sz, &digest, &merkle_buf);

  // Invert a char in the tree.
  merkle_buf.get()[0] = ~(merkle_buf.get()[0]);

  std::unique_ptr<BlobVerifier> verifier;
  ASSERT_OK(BlobVerifier::Create(std::move(digest), Metrics(), merkle_buf.get(), merkle_buf.size(),
                                 sz, nullptr, &verifier));

  EXPECT_EQ(verifier->Verify(buf.get(), sz, sz), ZX_ERR_IO_DATA_INTEGRITY);

  EXPECT_EQ(verifier->VerifyPartial(buf.get(), sz, 0, sz), ZX_ERR_IO_DATA_INTEGRITY);

  // Block-by-block -- everything fails
  for (size_t i = 0; i < sz; i += 8192) {
    EXPECT_EQ(verifier->VerifyPartial(buf.get() + i, 8192, i, 8192), ZX_ERR_IO_DATA_INTEGRITY);
  }
}

TEST_F(BlobVerifierTest, NonZeroTailCausesVerifyToFail) {
  constexpr int kBlobSize = 8000;
  uint8_t buf[kBlobfsBlockSize];
  FillWithRandom(buf, kBlobSize);
  // Zero the tail.
  memset(&buf[kBlobSize], 0, kBlobfsBlockSize - kBlobSize);

  fbl::Array<uint8_t> unused_merkle_buf;
  Digest digest;
  GenerateTree(buf, kBlobSize, &digest, &unused_merkle_buf);

  std::unique_ptr<BlobVerifier> verifier;
  EXPECT_OK(
      BlobVerifier::CreateWithoutTree(std::move(digest), Metrics(), kBlobSize, nullptr, &verifier));

  EXPECT_OK(verifier->Verify(buf, kBlobSize, sizeof(buf)));

  buf[kBlobSize] = 1;
  EXPECT_STATUS(verifier->Verify(buf, kBlobSize, sizeof(buf)), ZX_ERR_IO_DATA_INTEGRITY);
}

TEST_F(BlobVerifierTest, NonZeroTailCausesVerifyPartialToFail) {
  constexpr unsigned kBlobSize = (1 << 16) - 100;
  std::vector<uint8_t> buf(fbl::round_up(kBlobSize, kBlobfsBlockSize));
  FillWithRandom(buf.data(), kBlobSize);

  fbl::Array<uint8_t> merkle_buf;
  Digest digest;
  GenerateTree(buf.data(), kBlobSize, &digest, &merkle_buf);

  std::unique_ptr<BlobVerifier> verifier;
  ASSERT_OK(BlobVerifier::Create(std::move(digest), Metrics(), merkle_buf.get(), merkle_buf.size(),
                                 kBlobSize, nullptr, &verifier));

  constexpr int kVerifyOffset = kBlobSize - kBlobSize % kBlobfsBlockSize;
  EXPECT_OK(verifier->VerifyPartial(&buf[kVerifyOffset], kBlobSize - kVerifyOffset, kVerifyOffset,
                                    buf.size() - kVerifyOffset));

  buf[kBlobSize] = 1;
  EXPECT_STATUS(verifier->VerifyPartial(&buf[kVerifyOffset], kBlobSize - kVerifyOffset,
                                        kVerifyOffset, buf.size() - kVerifyOffset),
                ZX_ERR_IO_DATA_INTEGRITY);
}

}  // namespace
}  // namespace blobfs
