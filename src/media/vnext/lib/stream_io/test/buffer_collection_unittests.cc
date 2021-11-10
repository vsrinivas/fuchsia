// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media2/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/defer.h>
#include <lib/gtest/real_loop_fixture.h>

#include "src/media/vnext/lib/stream_io/buffer_collection.h"
#include "src/media/vnext/lib/stream_io/test/fake_buffer_provider.h"
#include "src/media/vnext/lib/threads/thread.h"

namespace fmlib::test {

constexpr uint32_t kRequestedBufferCount = 2;
constexpr uint32_t kExpectedBufferCount = kRequestedBufferCount + 1;
constexpr uint32_t kMinBufferSize = 1000;

class BufferCollectionUnitTest : public gtest::RealLoopFixture {
 public:
  BufferCollectionUnitTest() : thread_(fmlib::Thread::CreateForLoop(loop())) {
    buffer_provider_ = std::make_unique<FakeBufferProvider>();
  }

 protected:
  // Creates a pair of buffer collection tokens.
  static void CreateBufferCollectionTokens(zx::eventpair& provider_token_out,
                                           zx::eventpair& participant_token_out) {
    EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &provider_token_out, &participant_token_out));
  }

  // Returns a |fuchsia::media2::BufferConstraints| with |kRequestedBufferCount| and
  // |kMinBufferSize|.
  static fuchsia::media2::BufferConstraints SimpleConstraints() {
    fuchsia::media2::BufferConstraints constraints;
    constraints.set_buffer_count(kRequestedBufferCount);
    constraints.set_min_buffer_size(kMinBufferSize);
    return constraints;
  }

  Thread thread() { return thread_; }

  fuchsia::media2::BufferProvider& buffer_provider() { return *buffer_provider_; }

  // Creates a buffer collection.
  void CreateBufferCollection(zx::eventpair provider_token, bool& completed,
                              uint32_t expected_buffer_count = kExpectedBufferCount,
                              uint32_t expected_buffer_size = kMinBufferSize) {
    completed = false;
    buffer_provider_->CreateBufferCollection(
        std::move(provider_token), "output unittests",
        [&completed, expected_buffer_count, expected_buffer_size](
            fuchsia::media2::BufferProvider_CreateBufferCollection_Result result) mutable {
          EXPECT_TRUE(result.is_response());
          EXPECT_EQ(expected_buffer_count, result.response().collection_info.buffer_count());
          EXPECT_EQ(expected_buffer_size, result.response().collection_info.buffer_size());
          completed = true;
        });
  }

  std::unique_ptr<OutputBufferCollection> CreateOutputBufferCollection(
      uint32_t buffer_count = kRequestedBufferCount, uint32_t buffer_size = kMinBufferSize) {
    zx::eventpair provider_token;
    zx::eventpair participant_token;
    CreateBufferCollectionTokens(provider_token, participant_token);

    fuchsia::media2::BufferConstraints constraints;
    constraints.set_buffer_count(buffer_count);
    constraints.set_min_buffer_size(buffer_size);

    // Schedule creation of the buffer collection.
    std::unique_ptr<OutputBufferCollection> under_test;
    thread().schedule_task(
        OutputBufferCollection::Create(thread().executor(), buffer_provider(),
                                       std::move(participant_token), constraints, "testname", 0)
            .then([&under_test](fpromise::result<std::unique_ptr<OutputBufferCollection>,
                                                 fuchsia::media2::ConnectionError>& result) {
              EXPECT_TRUE(result.is_ok());
              under_test = result.take_value();
            }));
    RunLoopUntilIdle();
    // Because we haven't told the provider about the collection, the promise should not have
    // completed yet.
    EXPECT_FALSE(!!under_test);

    // Tell the provider to create the buffer collection.
    bool create_buffer_collection_completed = false;
    CreateBufferCollection(std::move(provider_token), create_buffer_collection_completed,
                           buffer_count + 1, buffer_size);
    RunLoopUntilIdle();
    EXPECT_TRUE(create_buffer_collection_completed);
    // Now the promise should have completed.
    EXPECT_TRUE(!!under_test);

    return under_test;
  }

  std::unique_ptr<InputBufferCollection> CreateInputBufferCollection(
      uint32_t buffer_count = kRequestedBufferCount, uint32_t buffer_size = kMinBufferSize) {
    zx::eventpair provider_token;
    zx::eventpair participant_token;
    CreateBufferCollectionTokens(provider_token, participant_token);

    fuchsia::media2::BufferConstraints constraints;
    constraints.set_buffer_count(buffer_count);
    constraints.set_min_buffer_size(buffer_size);

    // Schedule creation of the buffer collection.
    std::unique_ptr<InputBufferCollection> under_test;
    thread().schedule_task(
        InputBufferCollection::Create(buffer_provider(), std::move(participant_token), constraints,
                                      "testname", 0)
            .then([&under_test](fpromise::result<std::unique_ptr<InputBufferCollection>,
                                                 fuchsia::media2::ConnectionError>& result) {
              EXPECT_TRUE(result.is_ok());
              under_test = result.take_value();
            }));
    RunLoopUntilIdle();
    // Because we haven't told the provider about the collection, the promise should not have
    // completed yet.
    EXPECT_FALSE(!!under_test);

    // Tell the provider to create the buffer collection.
    bool create_buffer_collection_completed = false;
    CreateBufferCollection(std::move(provider_token), create_buffer_collection_completed,
                           buffer_count + 1, buffer_size);
    RunLoopUntilIdle();
    EXPECT_TRUE(create_buffer_collection_completed);
    // Now the promise should have completed.
    EXPECT_TRUE(!!under_test);

    return under_test;
  }

 private:
  Thread thread_;
  std::unique_ptr<fuchsia::media2::BufferProvider> buffer_provider_;
};

// Test |BufferCollection::DuplicateVmos|.
TEST_F(BufferCollectionUnitTest, DuplicateVmos) {
  std::unique_ptr<OutputBufferCollection> under_test = CreateOutputBufferCollection();

  auto dup_vmos = under_test->DuplicateVmos(ZX_RIGHT_SAME_RIGHTS);
  EXPECT_EQ(kExpectedBufferCount, dup_vmos.size());
  for (auto& dup_vmo : dup_vmos) {
    size_t size;
    EXPECT_EQ(ZX_OK, dup_vmo.get_size(&size));
    // VMOs must be at least kMinBufferSize bytes, but may be larger.
    EXPECT_LE(kMinBufferSize, size);
  }
}

// Test |OutputBufferCollection::AllocatePayloadBuffer|.
TEST_F(BufferCollectionUnitTest, AllocatePayloadBuffer) {
  std::unique_ptr<OutputBufferCollection> under_test = CreateOutputBufferCollection();

  // Allocate all the buffers.
  std::vector<PayloadBuffer> buffers(kExpectedBufferCount);
  for (auto& buffer : buffers) {
    buffer = under_test->AllocatePayloadBuffer(kMinBufferSize);
    EXPECT_TRUE(!!buffer);
    EXPECT_TRUE(!!buffer.data());
    EXPECT_EQ(kMinBufferSize, buffer.size());
  }

  // Try to allocate another - should fail.
  PayloadBuffer buffer = under_test->AllocatePayloadBuffer(kMinBufferSize);
  EXPECT_FALSE(!!buffer);

  // Free a buffer.
  buffers[0].Reset();
  RunLoopUntilIdle();

  // Allocate another - should succeed.
  buffers[0] = under_test->AllocatePayloadBuffer(kMinBufferSize);
  EXPECT_TRUE(!!buffers[0]);
  EXPECT_TRUE(!!buffers[0].data());
  EXPECT_EQ(kMinBufferSize, buffers[0].size());

  // Try to allocate another - should fail.
  buffer = under_test->AllocatePayloadBuffer(kMinBufferSize);
  EXPECT_FALSE(!!buffer);
}

// Test |OutputBufferCollection::AllocatePayloadBufferBlocking|.
TEST_F(BufferCollectionUnitTest, AllocatePayloadBufferBlocking) {
  std::unique_ptr<OutputBufferCollection> under_test = CreateOutputBufferCollection();

  // |AllocatePayloadBufferBlocking| cannot be called on the thread used by the collection.
  Thread other_thread = Thread::CreateNewThread("test AllocatePayloadBufferBlocking");
  std::vector<PayloadBuffer> buffers(kExpectedBufferCount);
  size_t state = 0;
  other_thread.PostTask([&under_test, &buffers, &state]() {
    for (auto& buffer : buffers) {
      buffer = under_test->AllocatePayloadBufferBlocking(kMinBufferSize);
      EXPECT_TRUE(!!buffer);
      EXPECT_TRUE(!!buffer.data());
      EXPECT_EQ(kMinBufferSize, buffer.size());
    }

    void* expected_next_data = buffers[0].data();
    state = 1;
    // This will block.
    PayloadBuffer buffer0 = under_test->AllocatePayloadBufferBlocking(kMinBufferSize);
    EXPECT_TRUE(!!buffer0);
    EXPECT_EQ(expected_next_data, buffer0.data());
    EXPECT_EQ(kMinBufferSize, buffer0.size());

    state = 2;
    // This will fail (because we call |FailPendingAllocation| below).
    PayloadBuffer buffer1 = under_test->AllocatePayloadBufferBlocking(kMinBufferSize);
    EXPECT_FALSE(!!buffer1);

    expected_next_data = buffers[1].data();
    state = 3;
    // This will block.
    PayloadBuffer buffer2 = under_test->AllocatePayloadBufferBlocking(kMinBufferSize);
    EXPECT_TRUE(!!buffer2);
    EXPECT_EQ(expected_next_data, buffer2.data());
    EXPECT_EQ(kMinBufferSize, buffer2.size());

    state = 4;
  });

  RunLoopUntil([&state]() { return state == 1; });

  // Release a buffer so the pending allocation will succeed.
  buffers[0].Reset();

  RunLoopUntil([&state]() { return state == 2; });

  // Fail the pending allocation.
  under_test->FailPendingAllocation();

  RunLoopUntil([&state]() { return state == 3; });

  // Release a buffer so the pending allocation will succeed.
  buffers[1].Reset();

  RunLoopUntil([&state]() { return state == 4; });
}

// Test |OutputBufferCollection::AllocatePayloadBufferWhenAvailable|.
TEST_F(BufferCollectionUnitTest, AllocatePayloadBufferWhenAvailable) {
  std::unique_ptr<OutputBufferCollection> under_test = CreateOutputBufferCollection();

  // Allocate all the buffers.
  std::vector<PayloadBuffer> buffers(kExpectedBufferCount);
  for (auto& buffer : buffers) {
    buffer = under_test->AllocatePayloadBuffer(kMinBufferSize);
    EXPECT_TRUE(!!buffer);
    EXPECT_TRUE(!!buffer.data());
    EXPECT_EQ(kMinBufferSize, buffer.size());
  }

  // Allocate a buffer when it becomes available.
  PayloadBuffer buffer;
  thread().schedule_task(under_test->AllocatePayloadBufferWhenAvailable(kMinBufferSize)
                             .then([&buffer](fpromise::result<PayloadBuffer>& result) {
                               EXPECT_TRUE(result.is_ok());
                               buffer = result.take_value();
                               EXPECT_TRUE(!!buffer);
                               EXPECT_TRUE(!!buffer.data());
                               EXPECT_EQ(kMinBufferSize, buffer.size());
                             }));

  // No buffer is available, so the promise should still be pending.
  RunLoopUntilIdle();
  EXPECT_FALSE(!!buffer);

  // Free a buffer, which should cause the promise above to complete.
  buffers[0].Reset();
  RunLoopUntil([&buffer]() { return !!buffer; });

  // Allocate a buffer when it becomes available. We will fail this one.
  bool failed = false;
  thread().schedule_task(under_test->AllocatePayloadBufferWhenAvailable(kMinBufferSize)
                             .then([&failed](fpromise::result<PayloadBuffer>& result) {
                               EXPECT_TRUE(result.is_ok());
                               EXPECT_FALSE(result.value());
                               failed = true;
                             }));

  // No buffer is available, so the promise should still be pending.
  RunLoopUntilIdle();
  EXPECT_FALSE(failed);

  // Fail the allocation.
  under_test->FailPendingAllocation();
  RunLoopUntil([&failed]() { return failed; });
}

// Test |OutputBufferCollection::GetFailPendingAllocationClosure|.
TEST_F(BufferCollectionUnitTest, GetFailPendingAllocationClosure) {
  fit::closure dead_letter;

  {
    std::unique_ptr<OutputBufferCollection> under_test = CreateOutputBufferCollection();

    // Allocate all the buffers.
    std::vector<PayloadBuffer> buffers(kExpectedBufferCount);
    for (auto& buffer : buffers) {
      buffer = under_test->AllocatePayloadBuffer(kMinBufferSize);
      EXPECT_TRUE(!!buffer);
      EXPECT_TRUE(!!buffer.data());
      EXPECT_EQ(kMinBufferSize, buffer.size());
    }

    // Allocate a buffer when it becomes available. We will fail this attempt.
    bool failed = false;
    thread().schedule_task(under_test->AllocatePayloadBufferWhenAvailable(kMinBufferSize)
                               .then([&failed](fpromise::result<PayloadBuffer>& result) {
                                 EXPECT_TRUE(result.is_ok());
                                 EXPECT_FALSE(result.value());
                                 failed = true;
                               }));

    // No buffer is available, so the promise should still be pending.
    RunLoopUntilIdle();
    EXPECT_FALSE(failed);

    // Fail the allocation using |GetFailPendingAllocationClosure|.
    under_test->GetFailPendingAllocationClosure()();
    RunLoopUntil([&failed]() { return failed; });

    // Save a closure to use after |under_test| goes out of scope.
    dead_letter = under_test->GetFailPendingAllocationClosure();
  }

  // Calling the closure after the collection is destroyed should be harmless.
  dead_letter();
  RunLoopUntilIdle();
}

// Test |InputBufferCollection::GetPayloadBuffer|.
TEST_F(BufferCollectionUnitTest, GetPayloadBuffer) {
  std::unique_ptr<InputBufferCollection> under_test = CreateInputBufferCollection();

  // Use |GetPayloadBuffer| to find the base address of each buffer.
  std::vector<void*> base_addresses(kExpectedBufferCount);
  uint32_t buffer_id = 0;
  for (auto& base_address : base_addresses) {
    PayloadBuffer buffer = under_test->GetPayloadBuffer(fuchsia::media2::PayloadRange{
        .buffer_id = buffer_id++, .offset = 0, .size = kMinBufferSize});
    EXPECT_TRUE(!!buffer);
    EXPECT_TRUE(!!buffer.data());
    EXPECT_EQ(kMinBufferSize, buffer.size());
    base_address = buffer.data();
  }

  // Try all guaranteed offset/size combinations, checking the |data| address each time.
  for (uint64_t offset = 0; offset < kMinBufferSize; ++offset) {
    for (uint32_t size = 1; size < kMinBufferSize - offset; ++size) {
      uint32_t buffer_id = 0;
      for (const auto& base_address : base_addresses) {
        PayloadBuffer buffer = under_test->GetPayloadBuffer(fuchsia::media2::PayloadRange{
            .buffer_id = buffer_id++, .offset = offset, .size = size});
        EXPECT_TRUE(!!buffer);
        EXPECT_EQ(reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(base_address) + offset),
                  buffer.data());
        EXPECT_EQ(size, buffer.size());
      }
    }
  }

  // Find the actual upper bound of the buffer size. This can exceed kMinBufferSize, because the
  // allocated VMOs can be larger than what we requested.
  uint32_t max_size = 0;
  for (uint32_t size = kMinBufferSize; true; ++size) {
    if (!under_test->GetPayloadBuffer(
            fuchsia::media2::PayloadRange{.buffer_id = 0, .offset = 0, .size = size})) {
      max_size = size - 1;
      break;
    }
  }

  EXPECT_LE(kMinBufferSize, max_size);

  // Verify that out-of-bounds requests fail.
  EXPECT_FALSE(!!under_test->GetPayloadBuffer(
      fuchsia::media2::PayloadRange{.buffer_id = kExpectedBufferCount, .offset = 0, .size = 1}));
  EXPECT_FALSE(!!under_test->GetPayloadBuffer(
      fuchsia::media2::PayloadRange{.buffer_id = 0, .offset = 0, .size = 0}));
  EXPECT_FALSE(!!under_test->GetPayloadBuffer(
      fuchsia::media2::PayloadRange{.buffer_id = 0, .offset = max_size, .size = 1}));
  EXPECT_FALSE(!!under_test->GetPayloadBuffer(
      fuchsia::media2::PayloadRange{.buffer_id = 0, .offset = max_size - 1, .size = 2}));
  EXPECT_FALSE(!!under_test->GetPayloadBuffer(
      fuchsia::media2::PayloadRange{.buffer_id = 0, .offset = 0, .size = max_size + 1}));
}

}  // namespace fmlib::test
