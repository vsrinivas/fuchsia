// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <blobfs/data-streamer.h>
#include <blobfs/journal/journal2.h>
#include <blobfs/writeback.h>
#include <zxtest/zxtest.h>

#include "utils.h"

namespace blobfs {
namespace {

class MockVmoidRegistry : public VmoidRegistry {
 private:
  zx_status_t AttachVmo(const zx::vmo& vmo, vmoid_t* out) override {
    *out = 5;
    return ZX_OK;
  }

  zx_status_t DetachVmo(vmoid_t vmoid) final { return ZX_OK; }
};

class MockTransactionHandler final : public fs::TransactionHandler {
 public:
  using TransactionCallback =
      fit::function<zx_status_t(const block_fifo_request_t* requests, size_t count)>;

  MockTransactionHandler() = default;

  ~MockTransactionHandler() { EXPECT_EQ(transactions_expected_, transactions_seen_); }

  void SetTransactionCallbacks(TransactionCallback* callbacks, size_t expected) {
    callbacks_ = callbacks;
    transactions_expected_ = expected;
    transactions_seen_ = 0;
  }

  uint32_t FsBlockSize() const final { return kBlobfsBlockSize; }

  groupid_t BlockGroupID() final { return 1; }

  uint32_t DeviceBlockSize() const final { return kBlobfsBlockSize; }

  zx_status_t Transaction(block_fifo_request_t* requests, size_t count) override {
    EXPECT_LT(transactions_seen_, transactions_expected_);
    return callbacks_[transactions_seen_++](requests, count);
  }

 private:
  TransactionCallback* callbacks_ = nullptr;
  size_t transactions_expected_ = 0;
  size_t transactions_seen_ = 0;
};

constexpr uint64_t kVmoOffset = 0;
constexpr uint64_t kDevOffset = 5;
constexpr uint64_t kWritebackLength = 8;
// This leaks an internal detail of the DataStreamer (the chunking size),
// but it's necssary to emulate this externally to validate the issued
// operations are chunked correctly.
constexpr uint64_t kMaxChunk = (3 * kWritebackLength) / 4;

class DataStreamerFixture : public zxtest::Test {
 public:
  void SetUp() override {
    std::unique_ptr<BlockingRingBuffer> journal_buffer;
    std::unique_ptr<BlockingRingBuffer> data_buffer;
    ASSERT_OK(
        BlockingRingBuffer::Create(&registry_, 10, "journal-writeback-buffer", &journal_buffer));
    ASSERT_OK(BlockingRingBuffer::Create(&registry_, kWritebackLength, "data-writeback-buffer",
                                         &data_buffer));
    auto info_block_buffer = std::make_unique<VmoBuffer>();
    constexpr size_t kInfoBlockBlockCount = 1;
    ASSERT_OK(info_block_buffer->Initialize(&registry_, kInfoBlockBlockCount, "info-block"));
    JournalSuperblock info_block = JournalSuperblock(std::move(info_block_buffer));
    info_block.Update(0, 0);

    journal_ = std::make_unique<Journal2>(&handler_, std::move(info_block),
                                          std::move(journal_buffer), std::move(data_buffer), 0);
  }

  MockTransactionHandler& handler() { return handler_; }
  std::unique_ptr<Journal2> take_journal() { return std::move(journal_); }

 private:
  MockVmoidRegistry registry_;
  MockTransactionHandler handler_;
  std::unique_ptr<Journal2> journal_;
};

using DataStreamerTest = DataStreamerFixture;

TEST_F(DataStreamerTest, StreamSmallOperationScheduledToWriteback) {
  const uint64_t kOperationLength = 2;
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kOperationLength * kBlockSize, 0, &vmo));
  UnbufferedOperationsBuilder builder;
  MockTransactionHandler::TransactionCallback callbacks[] = {
      [&](const block_fifo_request_t* requests, size_t count) {
        EXPECT_EQ(1, count);
        EXPECT_EQ(BLOCKIO_WRITE, requests[0].opcode);
        EXPECT_EQ(kDevOffset, requests[0].dev_offset);
        EXPECT_EQ(kOperationLength, requests[0].length);
        return ZX_OK;
      },
  };
  handler().SetTransactionCallbacks(callbacks, std::size(callbacks));
  {
    auto journal = take_journal();
    DataStreamer streamer(journal.get(), kWritebackLength);
    streamer.StreamData({.vmo = zx::unowned_vmo(vmo.get()),
                         {
                             .type = OperationType::kWrite,
                             .vmo_offset = kVmoOffset,
                             .dev_offset = kDevOffset,
                             .length = kOperationLength,
                         }});

    auto promise = streamer.Flush();
    // We can drop the promise; it has already been scheduled.
  }
}

TEST_F(DataStreamerTest, StreamOperationAsLargeAsWritebackIsChunked) {
  const uint64_t kOperationLength = kWritebackLength;

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kOperationLength * kBlockSize, 0, &vmo));
  UnbufferedOperationsBuilder builder;
  MockTransactionHandler::TransactionCallback callbacks[] = {
      [&](const block_fifo_request_t* requests, size_t count) {
        EXPECT_EQ(1, count);
        EXPECT_EQ(BLOCKIO_WRITE, requests[0].opcode);
        EXPECT_EQ(kDevOffset, requests[0].dev_offset);
        EXPECT_EQ(kMaxChunk, requests[0].length);
        return ZX_OK;
      },
      [&](const block_fifo_request_t* requests, size_t count) {
        EXPECT_EQ(1, count);
        EXPECT_EQ(BLOCKIO_WRITE, requests[0].opcode);
        EXPECT_EQ(kDevOffset + kMaxChunk, requests[0].dev_offset);
        EXPECT_EQ(kOperationLength - kMaxChunk, requests[0].length);
        return ZX_OK;
      },

  };
  handler().SetTransactionCallbacks(callbacks, std::size(callbacks));
  {
    auto journal = take_journal();
    DataStreamer streamer(journal.get(), kWritebackLength);
    streamer.StreamData({.vmo = zx::unowned_vmo(vmo.get()),
                         {
                             .type = OperationType::kWrite,
                             .vmo_offset = kVmoOffset,
                             .dev_offset = kDevOffset,
                             .length = kOperationLength,
                         }});

    auto promise = streamer.Flush();
    // We can drop the promise; it has already been scheduled.
  }
}

TEST_F(DataStreamerTest, StreamOperationLargerThanWritebackIsChunkedAndNonBlocking) {
  const uint64_t kOperationLength = kWritebackLength + 1;

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kOperationLength * kBlockSize, 0, &vmo));
  UnbufferedOperationsBuilder builder;
  MockTransactionHandler::TransactionCallback callbacks[] = {
      [&](const block_fifo_request_t* requests, size_t count) {
        EXPECT_EQ(1, count);
        EXPECT_EQ(BLOCKIO_WRITE, requests[0].opcode);
        EXPECT_EQ(kDevOffset, requests[0].dev_offset);
        EXPECT_EQ(kMaxChunk, requests[0].length);
        return ZX_OK;
      },
      [&](const block_fifo_request_t* requests, size_t count) {
        EXPECT_EQ(2, count);
        EXPECT_EQ(BLOCKIO_WRITE, requests[0].opcode);
        EXPECT_EQ(kDevOffset + kMaxChunk, requests[0].dev_offset);
        EXPECT_EQ(kWritebackLength - kMaxChunk, requests[0].length);
        EXPECT_EQ(BLOCKIO_WRITE, requests[1].opcode);
        EXPECT_EQ(kDevOffset + kOperationLength - 1, requests[1].dev_offset);
        EXPECT_EQ(1, requests[1].length);
        return ZX_OK;
      },

  };
  handler().SetTransactionCallbacks(callbacks, std::size(callbacks));
  {
    auto journal = take_journal();
    DataStreamer streamer(journal.get(), kWritebackLength);
    streamer.StreamData({.vmo = zx::unowned_vmo(vmo.get()),
                         {
                             .type = OperationType::kWrite,
                             .vmo_offset = kVmoOffset,
                             .dev_offset = kDevOffset,
                             .length = kOperationLength,
                         }});

    auto promise = streamer.Flush();
    // We can drop the promise; it has already been scheduled.
  }
}

TEST_F(DataStreamerTest, StreamManySmallOperationsAreMerged) {
  const uint64_t kOperationCount = 4;
  const uint64_t kOperationLength = 1;

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create((kOperationLength * kOperationCount) * kBlockSize, 0, &vmo));
  UnbufferedOperationsBuilder builder;
  MockTransactionHandler::TransactionCallback callbacks[] = {
      [&](const block_fifo_request_t* requests, size_t count) {
        EXPECT_EQ(1, count);
        EXPECT_EQ(BLOCKIO_WRITE, requests[0].opcode);
        EXPECT_EQ(kDevOffset, requests[0].dev_offset);
        EXPECT_EQ(kOperationCount * kOperationLength, requests[0].length);
        return ZX_OK;
      },
  };
  handler().SetTransactionCallbacks(callbacks, std::size(callbacks));
  {
    auto journal = take_journal();
    DataStreamer streamer(journal.get(), kWritebackLength);
    for (size_t i = 0; i < kOperationCount; i++) {
      UnbufferedOperation op = {.vmo = zx::unowned_vmo(vmo.get()),
                                {
                                    .type = OperationType::kWrite,
                                    .vmo_offset = kVmoOffset + i * kOperationLength,
                                    .dev_offset = kDevOffset + i * kOperationLength,
                                    .length = kOperationLength,
                                }};
      streamer.StreamData(std::move(op));
    }

    auto promise = streamer.Flush();
    // We can drop the promise; it has already been scheduled.
  }
}

TEST_F(DataStreamerTest, StreamFailedOperationFailsFlush) {
  const uint64_t kOperationLength = 1;

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kOperationLength * kBlockSize, 0, &vmo));
  UnbufferedOperationsBuilder builder;
  MockTransactionHandler::TransactionCallback callbacks[] = {
      [&](const block_fifo_request_t* requests, size_t count) { return ZX_ERR_INTERNAL; },
  };
  handler().SetTransactionCallbacks(callbacks, std::size(callbacks));
  bool failed_promise_observed = false;
  {
    auto journal = take_journal();
    DataStreamer streamer(journal.get(), kWritebackLength);
    UnbufferedOperation op = {.vmo = zx::unowned_vmo(vmo.get()),
                              {
                                  .type = OperationType::kWrite,
                                  .vmo_offset = kVmoOffset,
                                  .dev_offset = kDevOffset,
                                  .length = kOperationLength,
                              }};

    streamer.StreamData(std::move(op));
    journal->schedule_task(
        streamer.Flush().then([&](fit::context& context, fit::result<void, zx_status_t>& result) {
          EXPECT_TRUE(result.is_error());
          EXPECT_STATUS(ZX_ERR_INTERNAL, result.error());
          failed_promise_observed = true;
          return fit::ok();
        }));
  }
  EXPECT_TRUE(failed_promise_observed);
}

}  // namespace
}  // namespace blobfs
