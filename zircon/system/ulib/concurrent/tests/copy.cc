// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/concurrent/copy.h>

#include <array>
#include <random>

#include <zxtest/zxtest.h>
namespace test {

class ConcurrentCopyFixture : public zxtest::Test {
 public:
  ConcurrentCopyFixture() = default;
  virtual ~ConcurrentCopyFixture() = default;

  void SetUp() override {
    generator_.seed(kConstSeed);
    ResetBuffer();
  }

 protected:
  static constexpr size_t kTestBufferSize = 256;

  void ResetBuffer() {
    std::uniform_int_distribution<uint8_t> dist{0x00, 0x0FF};

    for (size_t i = 0; i < dst_.size(); ++i) {
      dst_[i] = dist(generator_);
      src_[i] = ~dst_[i];
    }
  }

  std::array<uint8_t, kTestBufferSize> src_{0};
  std::array<uint8_t, kTestBufferSize> dst_{0};

 private:
  static constexpr uint64_t kConstSeed = 0xa5f084a2c3de6b75;
  std::mt19937_64 generator_{kConstSeed};
};

TEST_F(ConcurrentCopyFixture, CopyTo) {
  using concurrent::SyncOpt;
  using concurrent::WellDefinedCopyTo;

  ASSERT_EQ(src_.size(), dst_.size());

  // Test all of the combinations of alignment at the start and end of the operation.
  for (size_t offset = 0; offset < sizeof(uint64_t); ++offset) {
    for (size_t remainder = 1; remainder <= sizeof(uint64_t); ++remainder) {
      const size_t op_len = src_.size() - offset - (sizeof(uint64_t) - remainder);

      ASSERT_LE(op_len + offset, src_.size());

      // Perform a copy-to using the default fence/element-atomic semantics.
      ResetBuffer();
      WellDefinedCopyTo(&dst_[offset], &src_[offset], op_len);
      ASSERT_BYTES_EQ(&dst_[offset], &src_[offset], op_len);

      // Perform a copy-to using release semantics for each element transfer,
      // and no fence, then check the results.
      ResetBuffer();
      WellDefinedCopyTo<SyncOpt::AcqRelOps>(&dst_[offset], &src_[offset], op_len);
      ASSERT_BYTES_EQ(&dst_[offset], &src_[offset], op_len);

      // Same test, but this time use a release fence at the start of the
      // operation, and relaxed atomic semantics on the individual element
      // transfers.
      ResetBuffer();
      WellDefinedCopyTo<SyncOpt::Fence>(&dst_[offset], &src_[offset], op_len);
      ASSERT_BYTES_EQ(&dst_[offset], &src_[offset], op_len);

      // Same test, but this time do not use either a fence or release semantics
      // on each element. Instead, simply do everything with relaxed atomic
      // stores.
      ResetBuffer();
      WellDefinedCopyTo<SyncOpt::None>(&dst_[offset], &src_[offset], op_len);
      ASSERT_BYTES_EQ(&dst_[offset], &src_[offset], op_len);
    }
  }

  // Finally, perform one more test using each of the fence options, but
  // guaranteeing that we have at least uint64_t alignment.
  using concurrent::internal::kMaxTransferGranularity;
  ASSERT_EQ(reinterpret_cast<uintptr_t>(&dst_) & (kMaxTransferGranularity - 1), 0);
  ASSERT_EQ(reinterpret_cast<uintptr_t>(&src_) & (kMaxTransferGranularity - 1), 0);

  // Release on the ops.
  ResetBuffer();
  WellDefinedCopyTo<SyncOpt::AcqRelOps, kMaxTransferGranularity>(&dst_, &src_, dst_.size());
  ASSERT_BYTES_EQ(&dst_, &src_, dst_.size());

  // Use a release fence before the transfer.
  ResetBuffer();
  WellDefinedCopyTo<SyncOpt::Fence, kMaxTransferGranularity>(&dst_, &src_, dst_.size());
  ASSERT_BYTES_EQ(&dst_, &src_, dst_.size());

  // Relaxed atomics on the ops, no fence.
  ResetBuffer();
  WellDefinedCopyTo<SyncOpt::None, kMaxTransferGranularity>(&dst_, &src_, dst_.size());
  ASSERT_BYTES_EQ(&dst_, &src_, dst_.size());
}

TEST_F(ConcurrentCopyFixture, CopyFrom) {
  using concurrent::SyncOpt;
  using concurrent::WellDefinedCopyFrom;

  ASSERT_EQ(src_.size(), dst_.size());

  // Test all of the combinations of alignment at the start and end of the operation.
  for (size_t offset = 0; offset < sizeof(uint64_t); ++offset) {
    for (size_t remainder = 1; remainder <= sizeof(uint64_t); ++remainder) {
      const size_t op_len = src_.size() - offset - (sizeof(uint64_t) - remainder);

      ASSERT_LE(op_len + offset, src_.size());

      // Perform a copy-from using the default fence/element-atomic semantics.
      ResetBuffer();
      WellDefinedCopyFrom(&dst_[offset], &src_[offset], op_len);
      ASSERT_BYTES_EQ(&dst_[offset], &src_[offset], op_len);

      // Perform a copy-from using acquire semantics for each element transfer,
      // and no fence, then check the results.
      ResetBuffer();
      WellDefinedCopyFrom<SyncOpt::AcqRelOps>(&dst_[offset], &src_[offset], op_len);
      ASSERT_BYTES_EQ(&dst_[offset], &src_[offset], op_len);

      // Same test, but this time use an acquire fence at the end of the
      // operation, and relaxed atomic semantics on the individual element
      // transfers.
      ResetBuffer();
      WellDefinedCopyFrom<SyncOpt::Fence>(&dst_[offset], &src_[offset], op_len);
      ASSERT_BYTES_EQ(&dst_[offset], &src_[offset], op_len);

      // Same test, but this time do not use either a fence or acquire semantics
      // on each element. Instead, simply do everything with relaxed atomic
      // loads.
      ResetBuffer();
      WellDefinedCopyFrom<SyncOpt::None>(&dst_[offset], &src_[offset], op_len);
      ASSERT_BYTES_EQ(&dst_[offset], &src_[offset], op_len);
    }
  }

  // Finally, perform one more test using each of the fence options, but
  // guaranteeing that we have at least uint64_t alignment.
  using concurrent::internal::kMaxTransferGranularity;
  ASSERT_EQ(reinterpret_cast<uintptr_t>(&dst_) & (kMaxTransferGranularity - 1), 0);
  ASSERT_EQ(reinterpret_cast<uintptr_t>(&src_) & (kMaxTransferGranularity - 1), 0);

  // Acquire on the ops.
  ResetBuffer();
  WellDefinedCopyFrom<SyncOpt::AcqRelOps, kMaxTransferGranularity>(&dst_, &src_, dst_.size());
  ASSERT_BYTES_EQ(&dst_, &src_, dst_.size());

  // Use an acquire fence after the transfer.
  ResetBuffer();
  WellDefinedCopyFrom<SyncOpt::Fence, kMaxTransferGranularity>(&dst_, &src_, dst_.size());
  ASSERT_BYTES_EQ(&dst_, &src_, dst_.size());

  // Relaxed atomics on the ops, no fence.
  ResetBuffer();
  WellDefinedCopyFrom<SyncOpt::None, kMaxTransferGranularity>(&dst_, &src_, dst_.size());
  ASSERT_BYTES_EQ(&dst_, &src_, dst_.size());
}

}  // namespace test
