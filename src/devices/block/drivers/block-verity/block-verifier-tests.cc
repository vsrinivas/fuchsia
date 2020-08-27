// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/task.h>
#include <lib/async/time.h>

#include <zxtest/zxtest.h>

#include "block-verifier.h"
#include "constants.h"

namespace {

// Chosen as the smallest number of blocks that force an integrity tree depth > 1
// for SHA256 on 4k blocks (1 superblock, 3 integrity blocks, 129 data blocks)
constexpr uint64_t kIntegrityBlocks = 3;
constexpr uint64_t kDataBlocks = 129;
constexpr uint64_t kBlockCount = 1 + kIntegrityBlocks + kDataBlocks;

// Our canonical test data will be 129 data blocks of all 0s and the three
// integrity blocks that would correctly authenticate such a volume:
// Integrity block 0: 128 copies of SHA256(4096 zero bytes)
// Integrity block 1: 1 copy of SHA256(4096 zero bytes), then 4064 zero bytes.
// Integrity block 2: SHA256(integrity block 0), SHA256(integrity block 1), 4032 zeroes
// We'll include appropriate constants below.

// Verify golden zero block hash with python:
// >>> zero_block = "\0" * 4096
constexpr uint8_t kZeroBlock[block_verity::kBlockSize] = {};
// >>> import hashlib
// >>> zero_block_hash = hashlib.sha256()
// >>> zero_block_hash.update(zero_block)
// >>> print(zero_block_hash.hexdigest())
// ad7facb2586fc6e966c004d7d1d16b024f5805ff7cb47c7a85dabd8b48892ca7
#define ZERO_BLOCK_HASH                                                                           \
  0xad, 0x7f, 0xac, 0xb2, 0x58, 0x6f, 0xc6, 0xe9, 0x66, 0xc0, 0x04, 0xd7, 0xd1, 0xd1, 0x6b, 0x02, \
      0x4f, 0x58, 0x05, 0xff, 0x7c, 0xb4, 0x7c, 0x7a, 0x85, 0xda, 0xbd, 0x8b, 0x48, 0x89, 0x2c,   \
      0xa7
// >>> iblock_zero = h.digest() * 128
#define SIXTEEN_ZERO_BLOCK_HASHES                                                          \
  ZERO_BLOCK_HASH, ZERO_BLOCK_HASH, ZERO_BLOCK_HASH, ZERO_BLOCK_HASH, ZERO_BLOCK_HASH,     \
      ZERO_BLOCK_HASH, ZERO_BLOCK_HASH, ZERO_BLOCK_HASH, ZERO_BLOCK_HASH, ZERO_BLOCK_HASH, \
      ZERO_BLOCK_HASH, ZERO_BLOCK_HASH, ZERO_BLOCK_HASH, ZERO_BLOCK_HASH, ZERO_BLOCK_HASH, \
      ZERO_BLOCK_HASH
constexpr uint8_t kIntegrityBlockZero[block_verity::kBlockSize] = {
    SIXTEEN_ZERO_BLOCK_HASHES, SIXTEEN_ZERO_BLOCK_HASHES, SIXTEEN_ZERO_BLOCK_HASHES,
    SIXTEEN_ZERO_BLOCK_HASHES, SIXTEEN_ZERO_BLOCK_HASHES, SIXTEEN_ZERO_BLOCK_HASHES,
    SIXTEEN_ZERO_BLOCK_HASHES, SIXTEEN_ZERO_BLOCK_HASHES,
};
// >>> iblock_zero_hash = hashlib.sha256()
// >>> iblock_zero_hash.update(iblock_zero)
// >>> print(iblock_zero_hash.hexdigest())
// b24a5dfc7087b09c7378bb9100b5ea913f283da2c8ca05297f39457cbdd651d4
#define INTEGRITY_BLOCK_ZERO_HASH                                                                 \
  0xb2, 0x4a, 0x5d, 0xfc, 0x70, 0x87, 0xb0, 0x9c, 0x73, 0x78, 0xbb, 0x91, 0x00, 0xb5, 0xea, 0x91, \
      0x3f, 0x28, 0x3d, 0xa2, 0xc8, 0xca, 0x05, 0x29, 0x7f, 0x39, 0x45, 0x7c, 0xbd, 0xd6, 0x51,   \
      0xd4
// >>> iblock_one = h.digest() + ("\0" * (4096 - len(h.digest()))
constexpr uint8_t kIntegrityBlockOne[block_verity::kBlockSize] = {ZERO_BLOCK_HASH};
// >>> iblock_one_hash = hashlib.sha256()
// >>> iblock_one_hash.update(iblock_one)
// >>> print(iblock_one_hash.hexdigest())
// ec8e469cd349676fea41eeeb5b70e45a30f9a058d862edc5823b95ddf135c801
#define INTEGRITY_BLOCK_ONE_HASH                                                                  \
  0xec, 0x8e, 0x46, 0x9c, 0xd3, 0x49, 0x67, 0x6f, 0xea, 0x41, 0xee, 0xeb, 0x5b, 0x70, 0xe4, 0x5a, \
      0x30, 0xf9, 0xa0, 0x58, 0xd8, 0x62, 0xed, 0xc5, 0x82, 0x3b, 0x95, 0xdd, 0xf1, 0x35, 0xc8,   \
      0x01
// >>> iblock_two = iblock_zero_hash.digest() + iblock_one_hash.digest() + ("\0" * (4096 - 32 - 32))
constexpr uint8_t kIntegrityBlockTwo[block_verity::kBlockSize] = {INTEGRITY_BLOCK_ZERO_HASH,
                                                                  INTEGRITY_BLOCK_ONE_HASH};
// >>> iblock_two_hash = hashlib.sha256()
// >>> iblock_two_hash.update(iblock_two)
// >>> print(iblock_two_hash.hexdigest())
// 3e5d285ca1f11edfca6327028471f08b75634ff3361264b88d79ee2e95cacb84
constexpr std::array<uint8_t, block_verity::kHashOutputSize> kRootHash(
    {0x3e, 0x5d, 0x28, 0x5c, 0xa1, 0xf1, 0x1e, 0xdf, 0xca, 0x63, 0x27,
     0x02, 0x84, 0x71, 0xf0, 0x8b, 0x75, 0x63, 0x4f, 0xf3, 0x36, 0x12,
     0x64, 0xb8, 0x8d, 0x79, 0xee, 0x2e, 0x95, 0xca, 0xcb, 0x84});

class TestBlockLoader : public block_verity::BlockLoaderInterface {
 public:
  void RequestBlocks(uint64_t start_block, uint64_t block_count, zx::vmo& vmo, void* cookie,
                     block_verity::BlockLoaderCallback callback) override {
    // Expect load from beginning of integrity section.
    ASSERT_EQ(start_block, 1);

    // Expect load of all integrity blocks (3).
    ASSERT_EQ(block_count, 3);

    // Fill out integrity section with appropriate contents
    ASSERT_OK(
        vmo.write(kIntegrityBlockZero, 0 * block_verity::kBlockSize, block_verity::kBlockSize));
    ASSERT_OK(
        vmo.write(kIntegrityBlockOne, 1 * block_verity::kBlockSize, block_verity::kBlockSize));
    ASSERT_OK(
        vmo.write(kIntegrityBlockTwo, 2 * block_verity::kBlockSize, block_verity::kBlockSize));

    // Report success.
    callback(cookie, ZX_OK);
  }
};

class FailLoadBlockLoader : public block_verity::BlockLoaderInterface {
 public:
  void RequestBlocks(uint64_t start_block, uint64_t block_count, zx::vmo& vmo, void* cookie,
                     block_verity::BlockLoaderCallback callback) override {
    callback(cookie, ZX_ERR_IO);
  }
};

block_verity::Geometry TestGeometry() {
  return block_verity::Geometry(block_verity::kBlockSize, block_verity::kHashOutputSize,
                                kBlockCount);
}

class CallbackSink {
 public:
  CallbackSink() : status(ZX_OK), called(false) {}
  void Callback(zx_status_t call_status) {
    called = true;
    status = call_status;
  }
  zx_status_t status;
  bool called = false;
};
void BlockVerifierCallbackSink(void* cookie, zx_status_t status) {
  CallbackSink* sink = static_cast<CallbackSink*>(cookie);
  sink->Callback(status);
}

TEST(BlockVerifierTest, PrepareAsyncSucceeds) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  TestBlockLoader bl;
  block_verity::BlockVerifier bv(TestGeometry(), kRootHash, &bl);
  CallbackSink sink;

  bv.PrepareAsync(&sink, BlockVerifierCallbackSink);
  loop.RunUntilIdle();
  ASSERT_EQ(true, sink.called);
  ASSERT_OK(sink.status);
}

TEST(BlockVerifierTest, PrepareAsyncFailsWhenBlockLoadFails) {
  // BlockVerifier should return failure when the underlying block loader
  // returns failure.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FailLoadBlockLoader bl;
  block_verity::BlockVerifier bv(TestGeometry(), kRootHash, &bl);
  CallbackSink sink;

  bv.PrepareAsync(&sink, BlockVerifierCallbackSink);
  loop.RunUntilIdle();
  ASSERT_EQ(true, sink.called);
  ASSERT_EQ(ZX_ERR_IO, sink.status);
}

class BlockVerifierTestFixture : public zxtest::Test {
 public:
  BlockVerifierTestFixture()
      : loop_(&kAsyncLoopConfigAttachToCurrentThread),
        bl_(),
        bv_(TestGeometry(), kRootHash, &bl_) {}
  void SetUp() override {
    // "Load" the integrity data
    CallbackSink sink;
    bv_.PrepareAsync(&sink, BlockVerifierCallbackSink);
    loop_.RunUntilIdle();
    ASSERT_EQ(true, sink.called);
    ASSERT_OK(sink.status);
  }

 protected:
  async::Loop loop_;
  TestBlockLoader bl_;
  block_verity::BlockVerifier bv_;
};

TEST_F(BlockVerifierTestFixture, VerifyZeroBlockSucceeds) {
  // Verifying the zero block should succeed for all data block offsets
  for (uint64_t data_index = 0; data_index < kDataBlocks; data_index++) {
    EXPECT_OK(bv_.VerifyDataBlockSync(data_index, kZeroBlock));
  }
}

TEST_F(BlockVerifierTestFixture, CorruptedDataBlockFails) {
  // Verifying a non-zero block should fail for all blocks
  uint8_t non_zero_block[block_verity::kBlockSize] = {0x01};
  for (uint64_t data_index = 0; data_index < kDataBlocks; data_index++) {
    EXPECT_EQ(ZX_ERR_IO_DATA_INTEGRITY, bv_.VerifyDataBlockSync(data_index, non_zero_block));
  }
}

TEST(BlockVerifierTest, CorruptRootHashFailsAllBlocks) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  TestBlockLoader bl;

  // Copy the correct root hash, then corrupt it.
  std::array<uint8_t, block_verity::kHashOutputSize> corrupted_root_hash;
  memcpy(corrupted_root_hash.data(), kRootHash.data(), block_verity::kHashOutputSize);
  corrupted_root_hash[0] = 0;  // changes the root hash

  block_verity::BlockVerifier bv(TestGeometry(), corrupted_root_hash, &bl);
  CallbackSink sink;

  bv.PrepareAsync(&sink, BlockVerifierCallbackSink);
  loop.RunUntilIdle();
  ASSERT_EQ(true, sink.called);
  ASSERT_OK(sink.status);

  // Every data block should fail the integrity check at the root
  for (uint64_t data_index = 0; data_index < kDataBlocks; data_index++) {
    EXPECT_EQ(ZX_ERR_IO_DATA_INTEGRITY, bv.VerifyDataBlockSync(data_index, kZeroBlock));
  }
}

TEST(BlockVerifierTest, CorruptedIntegrityBlockFailsAllCoveredChildren) {
  class CorruptingBlockLoader : public block_verity::BlockLoaderInterface {
   public:
    void RequestBlocks(uint64_t start_block, uint64_t block_count, zx::vmo& vmo, void* cookie,
                       block_verity::BlockLoaderCallback callback) override {
      // Expect load from beginning of integrity section.
      ASSERT_EQ(start_block, 1);

      // Expect load of all integrity blocks (3).
      ASSERT_EQ(block_count, 3);

      // Fill out integrity section with slightly modified contents:
      // * integrity block zero corrupts a bit at the beginning, and is replaced with
      // * integrity blocks one and two match the values expected for the seal.
      uint8_t corrupted_integrity_block_zero[block_verity::kBlockSize];
      memcpy(corrupted_integrity_block_zero, kIntegrityBlockZero, block_verity::kBlockSize);
      corrupted_integrity_block_zero[0] = 0;
      ASSERT_OK(vmo.write(corrupted_integrity_block_zero, 0 * block_verity::kBlockSize,
                          block_verity::kBlockSize));
      ASSERT_OK(
          vmo.write(kIntegrityBlockOne, 1 * block_verity::kBlockSize, block_verity::kBlockSize));
      ASSERT_OK(
          vmo.write(kIntegrityBlockTwo, 2 * block_verity::kBlockSize, block_verity::kBlockSize));

      // Report success.
      callback(cookie, ZX_OK);
    }
  };

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  CorruptingBlockLoader cbl;
  block_verity::BlockVerifier bv(TestGeometry(), kRootHash, &cbl);
  CallbackSink sink;

  bv.PrepareAsync(&sink, BlockVerifierCallbackSink);
  loop.RunUntilIdle();
  ASSERT_EQ(true, sink.called);
  ASSERT_OK(sink.status);

  // Based on corrupting one leaf integrity block, we expect to see 128 verify
  // failures and 1 verify success.
  for (uint64_t data_index = 0; data_index < kDataBlocks; data_index++) {
    zx_status_t rc = bv.VerifyDataBlockSync(data_index, kZeroBlock);
    if (data_index < kDataBlocks - 1) {
      EXPECT_EQ(ZX_ERR_IO_DATA_INTEGRITY, rc);
    } else {
      EXPECT_OK(rc);
    }
  }
}

TEST(BlockVerifierTest, CorruptedRootIntegrityBlockFailsAllReads) {
  class RootCorruptingBlockLoader : public block_verity::BlockLoaderInterface {
   public:
    void RequestBlocks(uint64_t start_block, uint64_t block_count, zx::vmo& vmo, void* cookie,
                       block_verity::BlockLoaderCallback callback) override {
      // Expect load from beginning of integrity section.
      ASSERT_EQ(start_block, 1);

      // Expect load of all integrity blocks (3).
      ASSERT_EQ(block_count, 3);

      // Corrupt integrity block 2 (the final/root integrity block)
      uint8_t corrupted_integrity_block_two[block_verity::kBlockSize];
      memcpy(corrupted_integrity_block_two, kIntegrityBlockTwo, block_verity::kBlockSize);
      corrupted_integrity_block_two[0] = 0;

      ASSERT_OK(
          vmo.write(kIntegrityBlockZero, 0 * block_verity::kBlockSize, block_verity::kBlockSize));
      ASSERT_OK(
          vmo.write(kIntegrityBlockOne, 1 * block_verity::kBlockSize, block_verity::kBlockSize));
      ASSERT_OK(vmo.write(corrupted_integrity_block_two, 2 * block_verity::kBlockSize,
                          block_verity::kBlockSize));
      // Report success.
      callback(cookie, ZX_OK);
    }
  };

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  RootCorruptingBlockLoader cbl;
  block_verity::BlockVerifier bv(TestGeometry(), kRootHash, &cbl);
  CallbackSink sink;

  bv.PrepareAsync(&sink, BlockVerifierCallbackSink);
  loop.RunUntilIdle();
  ASSERT_EQ(true, sink.called);
  ASSERT_OK(sink.status);

  // Based on corrupting the root integrity block, we expect to see all blocks
  // fail to verify, despite successful leaf verification, because the root does
  // not pass muster.
  for (uint64_t data_index = 0; data_index < kDataBlocks; data_index++) {
    EXPECT_EQ(ZX_ERR_IO_DATA_INTEGRITY, bv.VerifyDataBlockSync(data_index, kZeroBlock));
  }
}

}  // namespace
