// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fs/journal/data_streamer.h"

#include <zircon/assert.h>

#include <fs/journal/journal.h>
#include <fs/transaction/writeback.h>
#include <gtest/gtest.h>
#include <storage/buffer/vmoid_registry.h>

namespace fs {
namespace {

class MockVmoidRegistry : public storage::VmoidRegistry {
 private:
  zx_status_t BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out) override {
    *out = storage::Vmoid(5);
    return ZX_OK;
  }

  zx_status_t BlockDetachVmo(storage::Vmoid vmoid) final {
    EXPECT_EQ(5, vmoid.TakeId());
    return ZX_OK;
  }
};

class MockTransactionHandler final : public fs::TransactionHandler {
 public:
  using TransactionCallback =
      fit::function<zx_status_t(const std::vector<storage::BufferedOperation>& requests)>;

  MockTransactionHandler() = default;

  ~MockTransactionHandler() { EXPECT_EQ(transactions_expected_, transactions_seen_); }

  void SetTransactionCallbacks(TransactionCallback* callbacks, size_t expected) {
    callbacks_ = callbacks;
    transactions_expected_ = expected;
    transactions_seen_ = 0;
  }

  uint64_t BlockNumberToDevice(uint64_t block_num) const final { return block_num; }

  zx_status_t RunRequests(const std::vector<storage::BufferedOperation>& requests) override {
    EXPECT_LT(transactions_seen_, transactions_expected_);
    return callbacks_[transactions_seen_++](requests);
  }

 private:
  TransactionCallback* callbacks_ = nullptr;
  size_t transactions_expected_ = 0;
  size_t transactions_seen_ = 0;
};

constexpr uint64_t kBlockSize = kJournalBlockSize;
constexpr uint64_t kVmoOffset = 0;
constexpr uint64_t kDevOffset = 5;
constexpr uint64_t kWritebackLength = 8;
// This leaks an internal detail of the DataStreamer (the chunking size),
// but it's necssary to emulate this externally to validate the issued
// operations are chunked correctly.
constexpr uint64_t kMaxChunk = (3 * kWritebackLength) / 4;

class DataStreamerFixture : public testing::Test {
 public:
  void SetUp() override {
    std::unique_ptr<storage::BlockingRingBuffer> journal_buffer;
    std::unique_ptr<storage::BlockingRingBuffer> data_buffer;
    ASSERT_EQ(storage::BlockingRingBuffer::Create(&registry_, 10, kBlockSize,
                                                  "journal-writeback-buffer", &journal_buffer),
              ZX_OK);
    ASSERT_EQ(storage::BlockingRingBuffer::Create(&registry_, kWritebackLength, kBlockSize,
                                                  "data-writeback-buffer", &data_buffer),
              ZX_OK);
    auto info_block_buffer = std::make_unique<storage::VmoBuffer>();
    constexpr size_t kInfoBlockBlockCount = 1;
    ASSERT_EQ(
        info_block_buffer->Initialize(&registry_, kInfoBlockBlockCount, kBlockSize, "info-block"),
        ZX_OK);
    JournalSuperblock info_block = JournalSuperblock(std::move(info_block_buffer));
    info_block.Update(0, 0);

    journal_ =
        std::make_unique<Journal>(&handler_, std::move(info_block), std::move(journal_buffer),
                                  std::move(data_buffer), 0, Journal::Options());
  }

  MockTransactionHandler& handler() { return handler_; }
  std::unique_ptr<Journal> take_journal() { return std::move(journal_); }

 private:
  MockVmoidRegistry registry_;
  MockTransactionHandler handler_;
  std::unique_ptr<Journal> journal_;
};

using DataStreamerTest = DataStreamerFixture;

TEST_F(DataStreamerTest, StreamSmallOperationScheduledToWriteback) {
  const uint64_t kOperationLength = 2;
  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(kOperationLength * kBlockSize, 0, &vmo), ZX_OK);
  storage::UnbufferedOperationsBuilder builder;
  MockTransactionHandler::TransactionCallback callbacks[] = {
      [&](const std::vector<storage::BufferedOperation>& requests) {
        EXPECT_EQ(requests.size(), 1ul);
        EXPECT_EQ(storage::OperationType::kWrite, requests[0].op.type);
        EXPECT_EQ(kDevOffset, requests[0].op.dev_offset);
        EXPECT_EQ(kOperationLength, requests[0].op.length);
        return ZX_OK;
      },
  };
  handler().SetTransactionCallbacks(callbacks, std::size(callbacks));
  {
    auto journal = take_journal();
    DataStreamer streamer(journal.get(), kWritebackLength);
    streamer.StreamData({.vmo = zx::unowned_vmo(vmo.get()),
                         {
                             .type = storage::OperationType::kWrite,
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
  ASSERT_EQ(zx::vmo::create(kOperationLength * kBlockSize, 0, &vmo), ZX_OK);
  storage::UnbufferedOperationsBuilder builder;
  MockTransactionHandler::TransactionCallback callbacks[] = {
      [&](const std::vector<storage::BufferedOperation>& requests) {
        EXPECT_EQ(requests.size(), 1ul);
        EXPECT_EQ(storage::OperationType::kWrite, requests[0].op.type);
        EXPECT_EQ(kDevOffset, requests[0].op.dev_offset);
        EXPECT_EQ(kMaxChunk, requests[0].op.length);
        return ZX_OK;
      },
      [&](const std::vector<storage::BufferedOperation>& requests) {
        EXPECT_EQ(requests.size(), 1ul);
        EXPECT_EQ(storage::OperationType::kWrite, requests[0].op.type);
        EXPECT_EQ(kDevOffset + kMaxChunk, requests[0].op.dev_offset);
        EXPECT_EQ(kOperationLength - kMaxChunk, requests[0].op.length);
        return ZX_OK;
      },

  };
  handler().SetTransactionCallbacks(callbacks, std::size(callbacks));
  {
    auto journal = take_journal();
    DataStreamer streamer(journal.get(), kWritebackLength);
    streamer.StreamData({.vmo = zx::unowned_vmo(vmo.get()),
                         {
                             .type = storage::OperationType::kWrite,
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
  ASSERT_EQ(zx::vmo::create(kOperationLength * kBlockSize, 0, &vmo), ZX_OK);
  storage::UnbufferedOperationsBuilder builder;
  MockTransactionHandler::TransactionCallback callbacks[] = {
      [&](const std::vector<storage::BufferedOperation>& requests) {
        EXPECT_EQ(requests.size(), 1ul);
        EXPECT_EQ(storage::OperationType::kWrite, requests[0].op.type);
        EXPECT_EQ(kDevOffset, requests[0].op.dev_offset);
        EXPECT_EQ(kMaxChunk, requests[0].op.length);
        return ZX_OK;
      },
      [&](const std::vector<storage::BufferedOperation>& requests) {
        EXPECT_EQ(requests.size(), 2ul);
        EXPECT_EQ(storage::OperationType::kWrite, requests[0].op.type);
        EXPECT_EQ(kDevOffset + kMaxChunk, requests[0].op.dev_offset);
        EXPECT_EQ(kWritebackLength - kMaxChunk, requests[0].op.length);
        EXPECT_EQ(storage::OperationType::kWrite, requests[1].op.type);
        EXPECT_EQ(kDevOffset + kOperationLength - 1, requests[1].op.dev_offset);
        EXPECT_EQ(requests[1].op.length, 1ul);
        return ZX_OK;
      },

  };
  handler().SetTransactionCallbacks(callbacks, std::size(callbacks));
  {
    auto journal = take_journal();
    DataStreamer streamer(journal.get(), kWritebackLength);
    streamer.StreamData({.vmo = zx::unowned_vmo(vmo.get()),
                         {
                             .type = storage::OperationType::kWrite,
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
  ASSERT_EQ(zx::vmo::create((kOperationLength * kOperationCount) * kBlockSize, 0, &vmo), ZX_OK);
  storage::UnbufferedOperationsBuilder builder;
  MockTransactionHandler::TransactionCallback callbacks[] = {
      [&](const std::vector<storage::BufferedOperation>& requests) {
        EXPECT_EQ(requests.size(), 1ul);
        EXPECT_EQ(storage::OperationType::kWrite, requests[0].op.type);
        EXPECT_EQ(kDevOffset, requests[0].op.dev_offset);
        EXPECT_EQ(kOperationCount * kOperationLength, requests[0].op.length);
        return ZX_OK;
      },
  };
  handler().SetTransactionCallbacks(callbacks, std::size(callbacks));
  {
    auto journal = take_journal();
    DataStreamer streamer(journal.get(), kWritebackLength);
    for (size_t i = 0; i < kOperationCount; i++) {
      storage::UnbufferedOperation op = {.vmo = zx::unowned_vmo(vmo.get()),
                                         {
                                             .type = storage::OperationType::kWrite,
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
  ASSERT_EQ(zx::vmo::create(kOperationLength * kBlockSize, 0, &vmo), ZX_OK);
  storage::UnbufferedOperationsBuilder builder;
  MockTransactionHandler::TransactionCallback callbacks[] = {
      [&](const std::vector<storage::BufferedOperation>& requests) { return ZX_ERR_INTERNAL; },
  };
  handler().SetTransactionCallbacks(callbacks, std::size(callbacks));
  bool failed_promise_observed = false;
  {
    auto journal = take_journal();
    DataStreamer streamer(journal.get(), kWritebackLength);
    storage::UnbufferedOperation op = {.vmo = zx::unowned_vmo(vmo.get()),
                                       {
                                           .type = storage::OperationType::kWrite,
                                           .vmo_offset = kVmoOffset,
                                           .dev_offset = kDevOffset,
                                           .length = kOperationLength,
                                       }};

    streamer.StreamData(std::move(op));
    journal->schedule_task(
        streamer.Flush().then([&](fit::context& context, fit::result<void, zx_status_t>& result) {
          EXPECT_TRUE(result.is_error());
          EXPECT_EQ(result.error(), ZX_ERR_INTERNAL);
          failed_promise_observed = true;
          return fit::ok();
        }));
  }
  EXPECT_TRUE(failed_promise_observed);
}

}  // namespace
}  // namespace fs
