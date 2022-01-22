// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_runtime/channel.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/task.h>
#include <lib/fdf/cpp/channel_read.h>
#include <lib/fit/defer.h>
#include <lib/sync/completion.h>
#include <lib/zx/event.h>

#include <set>

#include <zxtest/zxtest.h>

#include "src/devices/bin/driver_runtime/arena.h"
#include "src/devices/bin/driver_runtime/dispatcher.h"
#include "src/devices/bin/driver_runtime/driver_context.h"
#include "src/devices/bin/driver_runtime/handle.h"
#include "src/devices/bin/driver_runtime/runtime_test_case.h"
#include "src/devices/bin/driver_runtime/test_utils.h"

class ChannelTest : public RuntimeTestCase {
 protected:
  ChannelTest() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  void SetUp() override;
  void TearDown() override;

  // Registers a wait_async request on |ch| and blocks until it is ready for reading.
  void WaitUntilReadReady(fdf_handle_t ch) {
    return RuntimeTestCase::WaitUntilReadReady(ch, fdf_dispatcher_);
  }

  // Allocates and populates an array of size |size|, containing test data. The array is owned by
  // |arena|.
  void AllocateTestData(fdf_arena_t* arena, size_t size, void** out_data);
  void AllocateTestDataWithStartValue(fdf_arena_t* arena, size_t size, size_t start_value,
                                      void** out_data);

  fdf::Channel local_;
  fdf::Channel remote_;

  fdf::Arena arena_;

  async::Loop loop_;
  fdf_dispatcher_t* fdf_dispatcher_;
};

void ChannelTest::SetUp() {
  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_OK(channels.status_value());
  local_ = std::move(channels->end0);
  remote_ = std::move(channels->end1);

  std::string_view tag{""};
  auto arena = fdf::Arena::Create(0, tag);
  ASSERT_OK(arena.status_value());
  arena_ = std::move(*arena);

  driver_runtime::Dispatcher* dispatcher;
  ASSERT_EQ(ZX_OK, driver_runtime::Dispatcher::CreateWithLoop(
                       FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, "scheduler_role", 0,
                       CreateFakeDriver(), &loop_, &dispatcher));

  fdf_dispatcher_ = static_cast<fdf_dispatcher_t*>(dispatcher);

  // Pretend all calls are non-reentrant so we don't have to worry about threading.
  driver_context::PushDriver(CreateFakeDriver());
}

void ChannelTest::TearDown() {
  local_.reset();
  remote_.reset();

  arena_.reset();

  fdf_dispatcher_destroy(fdf_dispatcher_);

  ASSERT_EQ(0, driver_runtime::gHandleTableArena.num_allocated());

  driver_context::PopDriver();
}

void ChannelTest::AllocateTestData(fdf_arena_t* arena, size_t size, void** out_data) {
  AllocateTestDataWithStartValue(arena, size, 0, out_data);
}

void ChannelTest::AllocateTestDataWithStartValue(fdf_arena_t* arena, size_t size,
                                                 size_t start_value, void** out_data) {
  uint32_t nums[size / sizeof(uint32_t)];
  for (uint32_t i = 0; i < size / sizeof(uint32_t); i++) {
    nums[i] = i;
  }
  void* data = arena->Allocate(size);
  ASSERT_NOT_NULL(data);
  memcpy(data, nums, size);
  *out_data = data;
}

TEST_F(ChannelTest, CreateAndDestroy) {}

TEST_F(ChannelTest, WriteReadEmptyMessage) {
  ASSERT_EQ(ZX_OK, fdf_channel_write(local_.get(), 0, nullptr, nullptr, 0, nullptr, 0));
  ASSERT_NO_FATAL_FAILURE(WaitUntilReadReady(remote_.get()));
  ASSERT_NO_FATAL_FAILURE(AssertRead(remote_.get(), nullptr, 0, nullptr, 0));
}

// Tests writing and reading an array of numbers.
TEST_F(ChannelTest, WriteData) {
  constexpr uint32_t kNumBytes = 24 * 1024;

  void* data;
  AllocateTestData(arena_.get(), kNumBytes, &data);

  ASSERT_EQ(ZX_OK, fdf_channel_write(local_.get(), 0, arena_.get(), data, kNumBytes, NULL, 0));
  ASSERT_NO_FATAL_FAILURE(WaitUntilReadReady(remote_.get()));
  ASSERT_NO_FATAL_FAILURE(AssertRead(remote_.get(), data, kNumBytes, nullptr, 0));
}

// Tests that transferring zircon handles are allowed.
TEST_F(ChannelTest, WriteZirconHandle) {
  zx::event event;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &event));

  void* handles_buf = arena_.Allocate(sizeof(fdf_handle_t));
  ASSERT_NOT_NULL(handles_buf);

  fdf_handle_t* handles = reinterpret_cast<fdf_handle_t*>(handles_buf);
  handles[0] = event.release();

  EXPECT_OK(fdf_channel_write(local_.get(), 0, arena_.get(), nullptr, 0, handles, 1));

  ASSERT_NO_FATAL_FAILURE(WaitUntilReadReady(remote_.get()));
  ASSERT_NO_FATAL_FAILURE(AssertRead(remote_.get(), nullptr, 0, handles, 1));
}

// Tests reading channel handles from a channel message, and writing to
// one of those handles.
TEST_F(ChannelTest, WriteToTransferredChannels) {
  // Create some channels to transfer.
  fdf_handle_t a0, a1;
  fdf_handle_t b0, b1;
  ASSERT_EQ(ZX_OK, fdf_channel_create(0, &a0, &a1));
  ASSERT_EQ(ZX_OK, fdf_channel_create(0, &b0, &b1));

  constexpr uint32_t kNumChannels = 2;
  size_t alloc_size = kNumChannels * sizeof(fdf_handle_t);
  auto channels_to_transfer = reinterpret_cast<fdf_handle_t*>(arena_.Allocate(alloc_size));
  ASSERT_NOT_NULL(channels_to_transfer);

  channels_to_transfer[0] = a1;
  channels_to_transfer[1] = b1;

  ASSERT_EQ(ZX_OK, fdf_channel_write(local_.get(), 0, arena_.get(), nullptr, 0,
                                     channels_to_transfer, kNumChannels));

  // Retrieve the transferred channels.
  ASSERT_NO_FATAL_FAILURE(WaitUntilReadReady(remote_.get()));
  fdf_arena_t* read_arena;
  zx_handle_t* handles;
  uint32_t num_handles;
  ASSERT_EQ(ZX_OK, fdf_channel_read(remote_.get(), 0, &read_arena, nullptr, nullptr, &handles,
                                    &num_handles));
  ASSERT_NOT_NULL(handles);
  ASSERT_EQ(num_handles, kNumChannels);

  // Write to the transferred channel.
  constexpr uint32_t kNumBytes = 4096;

  void* data;
  AllocateTestData(read_arena, kNumBytes, &data);
  ASSERT_EQ(ZX_OK, fdf_channel_write(handles[1], 0, read_arena, data, kNumBytes, NULL, 0));

  ASSERT_NO_FATAL_FAILURE(WaitUntilReadReady(b0));
  ASSERT_NO_FATAL_FAILURE(AssertRead(b0, data, kNumBytes, nullptr, 0));

  fdf_handle_close(a0);
  fdf_handle_close(a1);
  fdf_handle_close(b0);
  fdf_handle_close(b1);

  read_arena->Destroy();
}

// Tests waiting on a channel before a write happens.
TEST_F(ChannelTest, WaitAsyncBeforeWrite) {
  sync_completion_t read_completion;
  auto channel_read = std::make_unique<fdf::ChannelRead>(
      remote_.get(), 0,
      [&read_completion](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read,
                         fdf_status_t status) { sync_completion_signal(&read_completion); });
  ASSERT_OK(channel_read->Begin(fdf_dispatcher_));

  constexpr uint32_t kNumBytes = 4096;

  void* data;
  AllocateTestData(arena_.get(), kNumBytes, &data);
  ASSERT_EQ(ZX_OK, fdf_channel_write(local_.get(), 0, arena_.get(), data, kNumBytes, NULL, 0));

  sync_completion_wait(&read_completion, ZX_TIME_INFINITE);

  ASSERT_NO_FATAL_FAILURE(AssertRead(remote_.get(), data, kNumBytes, nullptr, 0));
}

// Tests reading multiple channel messages from within one read callback.
TEST_F(ChannelTest, ReadMultiple) {
  constexpr uint32_t kFirstMsgNumBytes = 128;
  constexpr uint32_t kSecondMsgNumBytes = 256;

  void* data;
  AllocateTestData(arena_.get(), kFirstMsgNumBytes, &data);
  ASSERT_EQ(ZX_OK,
            fdf_channel_write(local_.get(), 0, arena_.get(), data, kFirstMsgNumBytes, NULL, 0));

  void* data2;
  AllocateTestData(arena_.get(), kSecondMsgNumBytes, &data2);
  ASSERT_EQ(ZX_OK,
            fdf_channel_write(local_.get(), 0, arena_.get(), data2, kSecondMsgNumBytes, NULL, 0));

  sync_completion_t completion;

  auto channel_read = std::make_unique<fdf::ChannelRead>(
      remote_.get(), 0,
      [&](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        ASSERT_NO_FATAL_FAILURE(AssertRead(remote_.get(), data, kFirstMsgNumBytes, nullptr, 0));
        ASSERT_NO_FATAL_FAILURE(AssertRead(remote_.get(), data2, kSecondMsgNumBytes, nullptr, 0));
        // There should be no more messages.
        ASSERT_EQ(ZX_ERR_SHOULD_WAIT,
                  fdf_channel_read(remote_.get(), 0, nullptr, nullptr, nullptr, nullptr, nullptr));
        sync_completion_signal(&completion);
      });

  ASSERT_OK(channel_read->Begin(fdf_dispatcher_));

  sync_completion_wait(&completion, ZX_TIME_INFINITE);
}

// Tests reading and re-registering the wait async read handler multiple times.
TEST_F(ChannelTest, ReRegisterReadHandler) {
  constexpr size_t kNumReads = 10;
  constexpr uint32_t kDataSize = 128;

  // Populated below.
  std::array<std::array<uint8_t, kDataSize>, kNumReads> test_data;

  size_t completed_reads = 0;
  sync_completion_t completion;
  auto channel_read = std::make_unique<fdf::ChannelRead>(
      remote_.get(), 0,
      [&](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        ASSERT_NO_FATAL_FAILURE(
            AssertRead(remote_.get(), test_data[completed_reads].data(), kDataSize, nullptr, 0));
        completed_reads++;
        if (completed_reads == kNumReads) {
          sync_completion_signal(&completion);
        } else {
          ASSERT_OK(channel_read->Begin(fdf_dispatcher_));
        }
      });

  ASSERT_OK(channel_read->Begin(fdf_dispatcher_));

  for (size_t i = 0; i < kNumReads; i++) {
    void* data;
    AllocateTestDataWithStartValue(arena_.get(), kDataSize, i, &data);
    memcpy(test_data[i].data(), data, kDataSize);
    ASSERT_EQ(ZX_OK, fdf_channel_write(local_.get(), 0, arena_.get(), data, kDataSize, NULL, 0));
  }
  sync_completion_wait(&completion, ZX_TIME_INFINITE);
  ASSERT_EQ(completed_reads, kNumReads);
}

// Tests that we get a read call back if we had registered a read wait,
// and the peer closes.
TEST_F(ChannelTest, CloseSignalsPeerClosed) {
  sync_completion_t read_completion;
  auto channel_read = std::make_unique<fdf::ChannelRead>(
      remote_.get(), 0,
      [&read_completion](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read,
                         fdf_status_t status) {
        ASSERT_NOT_OK(status);
        sync_completion_signal(&read_completion);
      });
  ASSERT_OK(channel_read->Begin(fdf_dispatcher_));

  local_.reset();

  sync_completion_wait(&read_completion, ZX_TIME_INFINITE);
}

// Tests that we get a read call back if we had registered a read wait,
// and we close the channel.
TEST_F(ChannelTest, UnsyncDispatcherCallbackOnClose) {
  loop_.StartThread();

  driver_runtime::Dispatcher* async_dispatcher;
  ASSERT_EQ(ZX_OK, driver_runtime::Dispatcher::CreateWithLoop(FDF_DISPATCHER_OPTION_UNSYNCHRONIZED,
                                                              "", 0, CreateFakeDriver(), &loop_,
                                                              &async_dispatcher));

  sync_completion_t read_completion;
  auto channel_read = std::make_unique<fdf::ChannelRead>(
      remote_.get(), 0,
      [&read_completion](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read,
                         fdf_status_t status) {
        ASSERT_EQ(status, ZX_ERR_CANCELED);
        sync_completion_signal(&read_completion);
      });
  ASSERT_OK(channel_read->Begin(static_cast<fdf_dispatcher_t*>(async_dispatcher)));

  remote_.reset();

  sync_completion_wait(&read_completion, ZX_TIME_INFINITE);

  loop_.Quit();
  loop_.JoinThreads();

  fdf_dispatcher_destroy(static_cast<fdf_dispatcher_t*>(async_dispatcher));
}

TEST_F(ChannelTest, CancelSynchronousDispatcherCallbackOnClose) {
  const void* driver = CreateFakeDriver();
  driver_runtime::Dispatcher* sync_dispatcher;
  ASSERT_EQ(ZX_OK,
            driver_runtime::Dispatcher::CreateWithLoop(0, "", 0, driver, &loop_, &sync_dispatcher));

  ASSERT_EQ(ZX_OK, fdf_channel_write(local_.get(), 0, arena_.get(), nullptr, 0, nullptr, 0));

  // Make the read reentrant so that the callback will be queued on the async loop.
  driver_context::PushDriver(driver);
  auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });

  // Use the C API for this test, since we want to check Close() cancels the read,
  // and the C++ ChannelRead destructor would assert that the read had already been cancelled.
  fdf_channel_read_t channel_read;
  channel_read.handler = [](fdf_dispatcher_t* dispatcher, fdf_channel_read_t* read,
                            fdf_status_t status) {};
  channel_read.channel = remote_.get();
  // Since there is a pending message, this should queue a callback on the dispatcher.
  ASSERT_OK(
      fdf_channel_wait_async(static_cast<fdf_dispatcher*>(sync_dispatcher), &channel_read, 0));

  ASSERT_EQ(sync_dispatcher->callback_queue_size_slow(), 1);

  // Close the channel to trigger the cancellation.
  remote_.reset();

  ASSERT_EQ(sync_dispatcher->callback_queue_size_slow(), 0);

  fdf_dispatcher_destroy(static_cast<fdf_dispatcher_t*>(sync_dispatcher));
}

// Tests cancelling a channel read that has not yet been queued with the synchronized dispatcher.
TEST_F(ChannelTest, SyncDispatcherCancelUnqueuedRead) {
  const void* driver = CreateFakeDriver();
  driver_runtime::Dispatcher* sync_dispatcher;
  ASSERT_EQ(ZX_OK,
            driver_runtime::Dispatcher::CreateWithLoop(0, "", 0, driver, &loop_, &sync_dispatcher));

  auto channel_read = std::make_unique<fdf::ChannelRead>(
      remote_.get(), 0,
      [&](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        ASSERT_FALSE(true);  // This callback should never be called.
      });
  ASSERT_OK(channel_read->Begin(static_cast<fdf_dispatcher_t*>(sync_dispatcher)));

  channel_read->Cancel();

  fdf_dispatcher_destroy(static_cast<fdf_dispatcher_t*>(sync_dispatcher));
}

// Tests cancelling a channel read that has been queued with the synchronized dispatcher.
TEST_F(ChannelTest, SyncDispatcherCancelQueuedRead) {
  loop_.StartThread();

  const void* driver = CreateFakeDriver();
  driver_runtime::Dispatcher* sync_dispatcher;
  ASSERT_EQ(ZX_OK,
            driver_runtime::Dispatcher::CreateWithLoop(0, "", 0, driver, &loop_, &sync_dispatcher));

  // Make calls reentrant so any callback will be queued on the async loop.
  driver_context::PushDriver(driver);
  auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });

  auto channel_read = std::make_unique<fdf::ChannelRead>(
      remote_.get(), 0,
      [&](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        ASSERT_FALSE(true);  // This callback should never be called.
      });
  ASSERT_OK(channel_read->Begin(static_cast<fdf_dispatcher_t*>(sync_dispatcher)));

  sync_completion_t task_completion;
  ASSERT_OK(async::PostTask(sync_dispatcher->GetAsyncDispatcher(), [&] {
    // This should queue the callback on the async loop. It won't be running yet
    // as this task is currently blocking the loop.
    ASSERT_EQ(ZX_OK, fdf_channel_write(local_.get(), 0, arena_.get(), nullptr, 0, nullptr, 0));
    ASSERT_EQ(sync_dispatcher->callback_queue_size_slow(), 1);
    // This should synchronously cancel the callback.
    channel_read->Cancel();
    ASSERT_EQ(sync_dispatcher->callback_queue_size_slow(), 0);
    sync_completion_signal(&task_completion);
  }));

  ASSERT_OK(sync_completion_wait(&task_completion, ZX_TIME_INFINITE));

  // The read should already be cancelled, try registering a new one.
  sync_completion_t read_completion;
  channel_read = std::make_unique<fdf::ChannelRead>(
      remote_.get(), 0,
      [&read_completion](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read,
                         fdf_status_t status) { sync_completion_signal(&read_completion); });
  ASSERT_OK(channel_read->Begin(static_cast<fdf_dispatcher_t*>(sync_dispatcher)));

  ASSERT_OK(sync_completion_wait(&read_completion, ZX_TIME_INFINITE));

  loop_.Quit();
  loop_.JoinThreads();

  fdf_dispatcher_destroy(static_cast<fdf_dispatcher_t*>(sync_dispatcher));
}

// Tests cancelling a channel read that has not yet been queued with the unsynchronized dispatcher.
TEST_F(ChannelTest, UnsyncDispatcherCancelUnqueuedRead) {
  loop_.StartThread();

  const void* driver = CreateFakeDriver();
  driver_runtime::Dispatcher* unsync_dispatcher;
  ASSERT_EQ(ZX_OK,
            driver_runtime::Dispatcher::CreateWithLoop(FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, "", 0,
                                                       driver, &loop_, &unsync_dispatcher));

  // Make calls reentrant so that any callback will be queued on the async loop.
  driver_context::PushDriver(driver);
  auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });

  sync_completion_t completion;
  auto channel_read = std::make_unique<fdf::ChannelRead>(
      remote_.get(), 0,
      [&](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        ASSERT_EQ(status, ZX_ERR_CANCELED);
        sync_completion_signal(&completion);
      });
  ASSERT_OK(channel_read->Begin(static_cast<fdf_dispatcher_t*>(unsync_dispatcher)));

  channel_read->Cancel();
  ASSERT_OK(sync_completion_wait(&completion, ZX_TIME_INFINITE));

  loop_.Quit();
  loop_.JoinThreads();

  fdf_dispatcher_destroy(static_cast<fdf_dispatcher_t*>(unsync_dispatcher));
}

// Tests cancelling a channel read that has been queued with the unsynchronized dispatcher.
TEST_F(ChannelTest, UnsyncDispatcherCancelQueuedRead) {
  loop_.StartThread();

  const void* driver = CreateFakeDriver();
  driver_runtime::Dispatcher* unsync_dispatcher;
  ASSERT_EQ(ZX_OK,
            driver_runtime::Dispatcher::CreateWithLoop(FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, "", 0,
                                                       driver, &loop_, &unsync_dispatcher));

  // Make calls reentrant so that the callback will be queued on the async loop.
  driver_context::PushDriver(driver);
  auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });

  sync_completion_t read_completion;
  auto channel_read = std::make_unique<fdf::ChannelRead>(
      remote_.get(), 0,
      [&](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        ASSERT_OK(status);
        sync_completion_signal(&read_completion);
      });
  ASSERT_OK(channel_read->Begin(static_cast<fdf_dispatcher_t*>(unsync_dispatcher)));

  ASSERT_OK(async::PostTask(unsync_dispatcher->GetAsyncDispatcher(), [&] {
    // This should queue the callback on the async loop.
    ASSERT_EQ(ZX_OK, fdf_channel_write(local_.get(), 0, arena_.get(), nullptr, 0, nullptr, 0));
    ASSERT_EQ(unsync_dispatcher->callback_queue_size_slow(), 1);
    channel_read->Cancel();
    // The channel read is still expecting a callback.
    ASSERT_NOT_OK(channel_read->Begin(static_cast<fdf_dispatcher_t*>(unsync_dispatcher)));
  }));

  ASSERT_OK(sync_completion_wait(&read_completion, ZX_TIME_INFINITE));

  ASSERT_EQ(unsync_dispatcher->callback_queue_size_slow(), 0);

  // Try scheduling another read.
  sync_completion_reset(&read_completion);
  channel_read = std::make_unique<fdf::ChannelRead>(
      remote_.get(), 0,
      [&read_completion](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read,
                         fdf_status_t status) { sync_completion_signal(&read_completion); });
  ASSERT_OK(channel_read->Begin(static_cast<fdf_dispatcher_t*>(unsync_dispatcher)));

  ASSERT_OK(sync_completion_wait(&read_completion, ZX_TIME_INFINITE));

  loop_.Quit();
  loop_.JoinThreads();

  fdf_dispatcher_destroy(static_cast<fdf_dispatcher_t*>(unsync_dispatcher));
}

// Tests that you can wait on and read pending messages from a channel even if the peer is closed.
TEST_F(ChannelTest, ReadRemainingMessagesWhenPeerIsClosed) {
  void* data = arena_.Allocate(64);
  ASSERT_EQ(ZX_OK, fdf_channel_write(local_.get(), 0, arena_.get(), data, 64, NULL, 0));

  local_.reset();

  ASSERT_NO_FATAL_FAILURE(WaitUntilReadReady(remote_.get()));
  ASSERT_NO_FATAL_FAILURE(AssertRead(remote_.get(), data, 64, nullptr, 0));
}

// Tests that read provides ownership of an arena.
TEST_F(ChannelTest, ReadArenaOwnership) {
  void* data = arena_.Allocate(64);
  ASSERT_EQ(ZX_OK, fdf_channel_write(local_.get(), 0, arena_.get(), data, 64, NULL, 0));

  arena_.reset();

  fdf_arena_t* read_arena;
  ASSERT_NO_FATAL_FAILURE(WaitUntilReadReady(remote_.get()));
  ASSERT_NO_FATAL_FAILURE(AssertRead(remote_.get(), data, 64, nullptr, 0, &read_arena));

  // Re-use the arena provided by the read call.
  data = read_arena->Allocate(64);
  ASSERT_EQ(ZX_OK, fdf_channel_write(remote_.get(), 0, read_arena, data, 64, NULL, 0));

  fdf_arena_destroy(read_arena);

  ASSERT_NO_FATAL_FAILURE(WaitUntilReadReady(local_.get()));
  ASSERT_NO_FATAL_FAILURE(AssertRead(local_.get(), data, 64, nullptr, 0));
}

// This test was adapted from the Zircon Channel test of the same name.
TEST_F(ChannelTest, ConcurrentReadsConsumeUniqueElements) {
  // Used to force both threads to stall until both are ready to run.
  zx::event event;

  constexpr uint32_t kNumMessages = 2000;
  enum class ReadMessageStatus {
    kUnset,
    kReadFailed,
    kOk,
  };

  struct Message {
    uint64_t data = 0;
    uint32_t data_size = 0;
    ReadMessageStatus status = ReadMessageStatus::kUnset;
    fdf_arena_t* arena = nullptr;
  };

  std::vector<Message> read_messages;
  read_messages.resize(kNumMessages);

  auto reader_worker = [&](uint32_t offset) {
    zx_status_t wait_status = event.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr);
    if (wait_status != ZX_OK) {
      return;
    }
    for (uint32_t i = 0; i < kNumMessages / 2; ++i) {
      fdf_arena_t* arena;
      void* data;
      uint32_t read_bytes = 0;
      zx_status_t read_status =
          fdf_channel_read(remote_.get(), 0, &arena, &data, &read_bytes, nullptr, nullptr);
      uint32_t index = offset + i;
      auto& message = read_messages[index];
      if (read_status != ZX_OK) {
        message.status = ReadMessageStatus::kReadFailed;
        continue;
      }
      message.status = ReadMessageStatus::kOk;
      message.data = *(reinterpret_cast<uint64_t*>(data));
      message.data_size = read_bytes;
      message.arena = arena;
    }
    return;
  };

  ASSERT_OK(zx::event::create(0, &event));
  constexpr uint32_t kReader1Offset = 0;
  constexpr uint32_t kReader2Offset = kNumMessages / 2;
  {
    test_utils::AutoJoinThread worker_1(reader_worker, kReader1Offset);
    test_utils::AutoJoinThread worker_2(reader_worker, kReader2Offset);
    auto cleanup = fit::defer([&]() {
      // Notify cancelled.
      event.reset();
    });

    fdf_arena_t* arena;
    ASSERT_OK(fdf_arena_create(0, "", 0, &arena));
    for (uint64_t i = 1; i <= kNumMessages; ++i) {
      void* data = fdf_arena_allocate(arena, sizeof(i));
      memcpy(data, &i, sizeof(i));
      ASSERT_OK(fdf_channel_write(local_.get(), 0, arena, data, sizeof(i), nullptr, 0));
    }
    fdf_arena_destroy(arena);
    ASSERT_OK(event.signal(0, ZX_USER_SIGNAL_0));
    // Join before cleanup.
    worker_1.Join();
    worker_2.Join();
  }

  std::set<uint64_t> read_data;
  // Check that data os within (0, kNumMessages] range and that is monotonically increasing per
  // each reader.
  auto ValidateMessages = [&read_data, &read_messages, kNumMessages](uint32_t offset) {
    uint64_t prev = 0;
    for (uint32_t i = offset; i < kNumMessages / 2 + offset; ++i) {
      const auto& message = read_messages[i];
      read_data.insert(message.data);
      EXPECT_GT(message.data, 0);
      EXPECT_LE(message.data, kNumMessages);
      EXPECT_GT(message.data, prev);
      prev = message.data;
      EXPECT_EQ(message.data_size, sizeof(uint64_t));
      EXPECT_EQ(message.status, ReadMessageStatus::kOk);
    }
  };
  ValidateMessages(kReader1Offset);
  ValidateMessages(kReader2Offset);

  // No repeated messages.
  ASSERT_EQ(read_data.size(), kNumMessages,
            "Read messages do not match the number of written messages.");

  for (uint32_t i = 0; i < kNumMessages; i++) {
    fdf_arena_destroy(read_messages[i].arena);
  }
}

// Tests that handles in unread messages are closed when the channel is closed.
TEST_F(ChannelTest, OnFlightHandlesSignalledWhenPeerIsClosed) {
  zx::channel zx_on_flight_local;
  zx::channel zx_on_flight_remote;
  ASSERT_OK(zx::channel::create(0, &zx_on_flight_local, &zx_on_flight_remote));

  fdf_handle_t on_flight_local;
  fdf_handle_t on_flight_remote;
  ASSERT_EQ(ZX_OK, fdf_channel_create(0, &on_flight_local, &on_flight_remote));

  // Write the fdf channel |zx_on_flight_remote| from |local_| to |remote_|.
  zx_handle_t* channels_to_transfer =
      reinterpret_cast<zx_handle_t*>(arena_.Allocate(sizeof(zx_handle_t)));
  ASSERT_NOT_NULL(channels_to_transfer);
  *channels_to_transfer = zx_on_flight_remote.release();
  ASSERT_OK(fdf_channel_write(local_.get(), 0, arena_.get(), nullptr, 0, channels_to_transfer, 1));

  // Write the zircon channel |on_flight_remote| from |remote_| to |local_|.
  channels_to_transfer = channels_to_transfer =
      reinterpret_cast<zx_handle_t*>(arena_.Allocate(sizeof(zx_handle_t)));
  ASSERT_NOT_NULL(channels_to_transfer);
  *channels_to_transfer = on_flight_remote;
  ASSERT_OK(fdf_channel_write(remote_.get(), 0, arena_.get(), nullptr, 0, channels_to_transfer, 1));

  // Close |local_| and verify that |on_flight_local| gets a peer closed notification.
  sync_completion_t read_completion;
  auto channel_read_ = std::make_unique<fdf::ChannelRead>(
      on_flight_local, 0 /* options */,
      [&](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        sync_completion_signal(&read_completion);
      });
  EXPECT_EQ(ZX_OK, channel_read_->Begin(fdf_dispatcher_));

  local_.reset();

  sync_completion_wait(&read_completion, ZX_TIME_INFINITE);

  // Because |remote| is still not closed, |zx_on_flight_local| should still be writeable.
  zx_signals_t signals;
  ASSERT_EQ(zx_on_flight_local.wait_one(0, zx::time::infinite_past(), &signals), ZX_ERR_TIMED_OUT);
  ASSERT_NE(signals & ZX_CHANNEL_WRITABLE, 0);

  // Close |remote_| and verify that |zx_on_flight_local| gets a peer closed notification.
  remote_.reset();

  ASSERT_OK(zx_on_flight_local.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), nullptr));

  fdf_handle_close(on_flight_local);
}

// Nest 200 channels, each one in the payload of the previous one.
TEST_F(ChannelTest, NestingIsOk) {
  constexpr uint32_t kNestedCount = 200;
  std::vector<fdf_handle_t> local(kNestedCount);
  std::vector<fdf_handle_t> remote(kNestedCount);

  for (uint32_t i = 0; i < kNestedCount; i++) {
    ASSERT_OK(fdf_channel_create(0, &local[i], &remote[i]));
  }

  for (uint32_t i = kNestedCount - 1; i > 0; i--) {
    fdf_handle_t* handles =
        reinterpret_cast<fdf_handle_t*>(arena_.Allocate(2 * sizeof(fdf_handle_t)));
    ASSERT_NOT_NULL(handles);
    handles[0] = local[i];
    handles[1] = remote[i];
    ASSERT_OK(fdf_channel_write(local[i - 1], 0, arena_.get(), nullptr, 0, handles, 2));
  }

  // Close the handles and for destructions.
  fdf_handle_close(local[0]);
  fdf_handle_close(remote[0]);
}

//
// Channel::Call() test helpers.
//

// Describes a message to transfer for a channel call transaction.
// This is passed from the test to the server thread for verifying received messages.
class Message {
 public:
  static constexpr uint32_t kMaxDataSize = 64;

  // |data_size| specifies the size of the data to transfer, not including the txid.
  // |num_handles| specifies the number of handles to create and transfer.
  Message(uint32_t data_size, uint32_t num_handles)
      : data_size_(data_size), num_handles_(num_handles) {}

  // |handles| specifies the handles that should be transferred, rather than creating new ones.
  Message(uint32_t data_size, cpp20::span<zx_handle_t> handles)
      : handles_(handles),
        data_size_(data_size),
        num_handles_(static_cast<uint32_t>(handles.size())) {}

  // Writes a message to |channel|, with the data and handle buffers allocated using |arena|.
  fdf_status_t Write(const fdf::Channel& channel, const fdf::Arena& arena, fdf_txid_t txid) const;

  // Synchronously calls to |channel|, with the data and handle buffers allocated using |arena|.
  zx::status<fdf::Channel::ReadReturn> Call(const fdf::Channel& channel, const fdf::Arena& arena,
                                            zx::time deadline = zx::time::infinite()) const;

  // Returns whether |read| contains the expected data and number of handles.
  bool IsEquivalent(fdf::Channel::ReadReturn& read) const;

 private:
  // Allocates the fake data and handle buffers using |arena|.
  // This can be used to create the expected arguments for Channel::Call() / Channel::Write().
  // The returned data buffer will contain |txid| and |data|,
  // and the returned handles buffer will either contain newly constructed event objects,
  // or the |handles_| set by the Message constructor.
  fdf_status_t AllocateBuffers(const fdf::Arena& arena, fdf_txid_t txid, void** out_data,
                               uint32_t* out_num_bytes,
                               cpp20::span<zx_handle_t>* out_handles) const;

  uint32_t data_[kMaxDataSize] = {0};
  cpp20::span<zx_handle_t> handles_;
  uint32_t data_size_;
  uint32_t num_handles_;
};

fdf_status_t Message::Write(const fdf::Channel& channel, const fdf::Arena& arena,
                            fdf_txid_t txid) const {
  void* data = nullptr;
  uint32_t num_bytes = 0;
  cpp20::span<zx_handle_t> handles;
  fdf_status_t status = AllocateBuffers(arena, txid, &data, &num_bytes, &handles);
  if (status != ZX_OK) {
    return status;
  }
  auto write_status = channel.Write(0, arena, data, num_bytes, std::move(handles));
  return write_status.status_value();
}

// Synchronously calls to |channel|, with the data and handle buffers allocated using |arena|.
zx::status<fdf::Channel::ReadReturn> Message::Call(const fdf::Channel& channel,
                                                   const fdf::Arena& arena,
                                                   zx::time deadline) const {
  void* data = nullptr;
  uint32_t num_bytes = 0;
  cpp20::span<zx_handle_t> handles;
  fdf_status_t status = AllocateBuffers(arena, 0, &data, &num_bytes, &handles);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return channel.Call(0, deadline, arena, data, num_bytes, std::move(handles));
}

// Allocates the fake data and handle buffers using |arena|.
// This can be used to create the expected arguments for Channel::Call() / Channel::Write().
// The returned data buffer will contain |txid| and |data|,
// and the returned handles buffer will either contain newly constructed event objects,
// or the |handles_| set by the Message constructor.
fdf_status_t Message::AllocateBuffers(const fdf::Arena& arena, fdf_txid_t txid, void** out_data,
                                      uint32_t* out_num_bytes,
                                      cpp20::span<zx_handle_t>* out_handles) const {
  uint32_t total_size = sizeof(fdf_txid_t) + data_size_;

  void* bytes = arena.Allocate(total_size);
  if (!bytes) {
    return ZX_ERR_NO_MEMORY;
  }
  memcpy(bytes, &txid, sizeof(txid));
  memcpy(static_cast<uint8_t*>(bytes) + sizeof(txid), data_, data_size_);

  void* handles_bytes = arena.Allocate(num_handles_ * sizeof(fdf_handle_t));
  if (!handles_bytes) {
    return ZX_ERR_NO_MEMORY;
  }
  fdf_handle_t* handles = static_cast<fdf_handle_t*>(handles_bytes);

  uint32_t i = 0;
  auto cleanup = fit::defer([&i, handles]() {
    for (uint32_t j = 0; j < i; j++) {
      zx_handle_close(handles[j]);
    }
  });
  for (i = 0; i < num_handles_; i++) {
    if (handles_.size() > i) {
      handles[i] = handles_[i];
    } else {
      zx::event event;
      zx_status_t status = zx::event::create(0, &event);
      if (status != ZX_OK) {
        return status;
      }
      handles[i] = event.release();
    }
  }
  cleanup.cancel();

  cpp20::span<zx_handle_t> handles_span{handles, num_handles_};
  *out_data = bytes;
  *out_num_bytes = total_size;
  *out_handles = std::move(handles_span);
  return ZX_OK;
}

// Returns whether |read| contains the expected data and number of handles.
bool Message::IsEquivalent(fdf::Channel::ReadReturn& read) const {
  if ((data_size_ + sizeof(fdf_txid_t)) != read.num_bytes) {
    return false;
  }
  uint8_t* read_data_start = static_cast<uint8_t*>(read.data) + sizeof(fdf_txid_t);
  if (memcmp(data_, read_data_start, data_size_) != 0) {
    return false;
  }
  if (num_handles_ != read.handles.size()) {
    return false;
  }
  return true;
}

void CloseHandles(const fdf::Channel::ReadReturn& read) {
  for (const auto& h : read.handles) {
    if (driver_runtime::Handle::IsFdfHandle(h)) {
      fdf_handle_close(h);
    } else {
      zx_handle_close(h);
    }
  }
}

// Server implementation for channel call tests.
// Waits for |message_count| messages, and replies to the messages if |accumulated_messages|
// mnumber of messages has been received.
// If |wait_for_event| is provided, waits for the event to be signaled before returning.
template <uint32_t reply_data_size, uint32_t reply_handle_count, uint32_t accumulated_messages>
void ReplyAndWait(const Message& request, uint32_t message_count, fdf::Channel svc,
                  async::Loop* process_loop, std::atomic<const char*>* error,
                  zx::event* wait_for_event) {
  // Make a separate dispatcher for the server.
  int fake_driver;  // For creating a fake pointer to the driver.
  driver_runtime::Dispatcher* dispatcher;
  ASSERT_EQ(ZX_OK, driver_runtime::Dispatcher::CreateWithLoop(0, "scheduler_role", 0,
                                                              reinterpret_cast<void*>(&fake_driver),
                                                              process_loop, &dispatcher));
  auto fdf_dispatcher = static_cast<fdf_dispatcher_t*>(dispatcher);

  process_loop->StartThread();

  // TODO(fxbug.dev/87840): we should use Dispatcher::Destroy once implemented.
  auto shutdown = fit::defer([&]() {
    process_loop->Quit();
    process_loop->JoinThreads();
  });

  std::set<fdf_txid_t> live_ids;
  std::vector<fdf::Channel::ReadReturn> live_requests;

  for (uint32_t i = 0; i < message_count; ++i) {
    ASSERT_NO_FATAL_FAILURE(RuntimeTestCase::WaitUntilReadReady(svc.get(), fdf_dispatcher));
    auto read_return = svc.Read(0);
    if (read_return.is_error()) {
      *error = "Failed to read request.";
      return;
    }
    if (!request.IsEquivalent(*read_return)) {
      *error = "Failed to validate request.";
      return;
    }

    CloseHandles(*read_return);

    fdf_txid_t txid = *static_cast<fdf_txid_t*>(read_return->data);
    if (live_ids.find(txid) != live_ids.end()) {
      *error = "Repeated id used for live transaction.";
      return;
    }
    live_ids.insert(txid);
    live_requests.push_back(std::move(*read_return));
    if (live_requests.size() < accumulated_messages) {
      continue;
    }

    // We've collected |accumulated_messages|, so we reply to all pending messages.
    for (const auto& req : live_requests) {
      fdf_txid_t txid = *static_cast<fdf_txid_t*>(req.data);

      Message reply = Message(reply_data_size, reply_handle_count);
      fdf_status_t status = reply.Write(svc, req.arena, txid);
      if (status != ZX_OK) {
        *error = "Failed to write reply.";
        return;
      }
    }
    live_requests.clear();
  }

  if (wait_for_event != nullptr) {
    if (wait_for_event->wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr) != ZX_OK) {
      *error = "Failed to wait for signal event.";
      return;
    }
  }

  fdf_dispatcher_destroy(static_cast<fdf_dispatcher_t*>(dispatcher));
}

template <uint32_t reply_data_size, uint32_t reply_handle_count, uint32_t accumulated_messages = 0>
void Reply(const Message& request, uint32_t message_count, fdf::Channel svc,
           async::Loop* process_loop, std::atomic<const char*>* error) {
  ReplyAndWait<reply_data_size, reply_handle_count, accumulated_messages>(
      request, message_count, std::move(svc), process_loop, error, nullptr);
}

template <uint32_t reply_data_size, uint32_t reply_handle_count>
void SuccessfulChannelCall(fdf::Channel local, fdf::Channel remote, async::Loop* process_loop,
                           const fdf::Arena& arena, const Message& request) {
  std::atomic<const char*> error = nullptr;

  {
    test_utils::AutoJoinThread service_thread(Reply<reply_data_size, reply_handle_count>, request,
                                              1, std::move(remote), process_loop, &error);
    auto read = request.Call(local, arena);
    ASSERT_OK(read.status_value());
    ASSERT_EQ(read->num_bytes, sizeof(fdf_txid_t) + reply_data_size);
    ASSERT_EQ(read->handles.size(), reply_handle_count);
    CloseHandles(*read);
  }
  if (error != nullptr) {
    FAIL("Service Thread reported error: %s\n", error.load());
  }
}

//
// Tests for fdf_channel_call
//

TEST_F(ChannelTest, CallBytesFitIsOk) {
  constexpr uint32_t kReplyDataSize = 5;
  constexpr uint32_t kReplyHandleCount = 0;

  Message request(4, 0);

  ASSERT_NO_FATAL_FAILURE((SuccessfulChannelCall<kReplyDataSize, kReplyHandleCount>(
      std::move(local_), std::move(remote_), &loop_, arena_, request)));
}

TEST_F(ChannelTest, CallHandlesFitIsOk) {
  constexpr uint32_t kReplyDataSize = 0;
  constexpr uint32_t kReplyHandleCount = 2;

  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));

  zx_handle_t handles[1] = {event.release()};

  Message request = Message(0, handles);

  ASSERT_NO_FATAL_FAILURE((SuccessfulChannelCall<kReplyDataSize, kReplyHandleCount>(
      std::move(local_), std::move(remote_), &loop_, arena_, request)));
}

TEST_F(ChannelTest, CallHandleAndBytesFitsIsOk) {
  constexpr uint32_t kReplyDataSize = 2;
  constexpr uint32_t kReplyHandleCount = 2;

  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));

  zx_handle_t handles[1] = {event.release()};

  Message request = Message(2, handles);

  ASSERT_NO_FATAL_FAILURE((SuccessfulChannelCall<kReplyDataSize, kReplyHandleCount>(
      std::move(local_), std::move(remote_), &loop_, arena_, request)));
}

TEST_F(ChannelTest, CallManagedThreadAllowsSyncCalls) {
  static constexpr uint32_t kNumBytes = 4;
  void* data = arena_.Allocate(kNumBytes);
  ASSERT_EQ(ZX_OK, fdf_channel_write(local_.get(), 0, arena_.get(), data, kNumBytes, NULL, 0));

  // Create a dispatcher that allows sync calls.
  auto driver = CreateFakeDriver();
  driver_runtime::Dispatcher* allow_sync_calls_dispatcher;
  ASSERT_EQ(ZX_OK, driver_runtime::Dispatcher::CreateWithLoop(
                       FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS, "", 0, driver, &loop_,
                       &allow_sync_calls_dispatcher));

  // TODO(fxbug.dev/87840): we should use Dispatcher::Destroy once implemented.
  auto shutdown = fit::defer([&]() {
    loop_.Quit();
    loop_.JoinThreads();
  });

  // Signaled once the Channel::Call completes.
  sync_completion_t call_complete;

  auto sync_channel_read = std::make_unique<fdf::ChannelRead>(
      remote_.get(), 0,
      [&](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        // This is now running on a managed thread that allows sync calls.
        fdf::UnownedChannel unowned(channel_read->channel());

        auto read = unowned->Read(0);
        ASSERT_OK(read.status_value());

        auto call = unowned->Call(0, zx::time::infinite(), arena_, data, kNumBytes,
                                  cpp20::span<zx_handle_t>());
        ASSERT_OK(call.status_value());
        sync_completion_signal(&call_complete);
      });
  {
    // Make the call non-reentrant.
    // This will still run the callback on an async thread, as the dispatcher allows sync calls.
    driver_context::PushDriver(driver);
    auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });
    ASSERT_OK(sync_channel_read->Begin(static_cast<fdf_dispatcher*>(allow_sync_calls_dispatcher)));
  }

  // Wait for the call request and reply.
  auto channel_read = std::make_unique<fdf::ChannelRead>(
      local_.get(), 0,
      [&](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        fdf::UnownedChannel unowned(channel_read->channel());

        auto read = unowned->Read(0);
        ASSERT_OK(read.status_value());

        ASSERT_EQ(read->num_bytes, kNumBytes);
        fdf_txid_t* txid = static_cast<fdf_txid_t*>(read->data);

        // Write a reply with the same txid.
        void* reply = read->arena.Allocate(sizeof(fdf_txid_t));
        memcpy(reply, txid, sizeof(fdf_txid_t));
        auto write =
            unowned->Write(0, read->arena, reply, sizeof(fdf_txid_t), cpp20::span<zx_handle_t>());
        ASSERT_OK(write.status_value());
      });
  ASSERT_OK(channel_read->Begin(fdf_dispatcher_));

  sync_completion_wait(&call_complete, ZX_TIME_INFINITE);

  fdf_dispatcher_destroy(static_cast<fdf_dispatcher_t*>(allow_sync_calls_dispatcher));
}

TEST_F(ChannelTest, CallPendingTransactionsUseDifferentIds) {
  constexpr uint32_t kReplyDataSize = 0;
  constexpr uint32_t kReplyHandleCount = 0;
  // The service thread will wait until |kAcummulatedMessages| have been read from the channel
  // before replying in the same order they came through.
  constexpr uint32_t kAccumulatedMessages = 20;

  std::atomic<const char*> error = nullptr;
  std::vector<zx_status_t> call_result(kAccumulatedMessages, ZX_OK);

  Message request(2, 0);

  fdf::Channel local = std::move(local_);

  {
    test_utils::AutoJoinThread service_thread(
        Reply<kReplyDataSize, kReplyHandleCount, kAccumulatedMessages>, request,
        kAccumulatedMessages, std::move(remote_), &loop_, &error);

    std::vector<test_utils::AutoJoinThread> calling_threads;
    calling_threads.reserve(kAccumulatedMessages);
    for (uint32_t i = 0; i < kAccumulatedMessages; ++i) {
      calling_threads.push_back(
          test_utils::AutoJoinThread([i, &call_result, &local, &request, this]() {
            auto read_return = request.Call(local, arena_);
            call_result[i] = read_return.status_value();
          }));
    }
  }

  for (auto call_status : call_result) {
    EXPECT_OK(call_status, "channel::call failed in client thread.");
  }

  if (error != nullptr) {
    FAIL("Service Thread reported error: %s\n", error.load());
  }
}

TEST_F(ChannelTest, CallDeadlineExceededReturnsTimedOut) {
  constexpr uint32_t kReplyDataSize = 0;
  constexpr uint32_t kReplyHandleCount = 0;
  constexpr uint32_t kAccumulatedMessages = 2;

  std::atomic<const char*> error = nullptr;
  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));

  Message request = Message(2, 0);
  {
    // We pass in accumulated_messages > message_count, so the server will read
    // the message without replying.
    test_utils::AutoJoinThread service_thread(
        ReplyAndWait<kReplyDataSize, kReplyHandleCount, kAccumulatedMessages>, request,
        kAccumulatedMessages - 1 /* message_count */, std::move(remote_), &loop_, &error, &event);
    auto read_return = request.Call(local_, arena_, zx::time::infinite_past());
    ASSERT_EQ(ZX_ERR_TIMED_OUT, read_return.status_value());
    // Signal the server to quit.
    event.signal(0, ZX_USER_SIGNAL_0);
  }

  if (error != nullptr) {
    FAIL("Service Thread reported error: %s\n", error.load());
  }
}

//
// Tests for fdf_channel_write error conditions
//

TEST_F(ChannelTest, WriteToClosedHandle) {
  local_.reset();

  ASSERT_DEATH([&] {
    EXPECT_EQ(ZX_ERR_BAD_HANDLE,
              fdf_channel_write(local_.get(), 0, nullptr, nullptr, 0, nullptr, 0));
  });
}

// Tests providing a close handle as part of a channel message.
TEST_F(ChannelTest, WriteClosedHandle) {
  fdf_handle_t closed_ch;
  fdf_handle_t additional_ch;
  ASSERT_EQ(ZX_OK, fdf_channel_create(0, &closed_ch, &additional_ch));
  fdf_handle_close(closed_ch);

  void* handles_buf = arena_.Allocate(sizeof(fdf_handle_t));
  ASSERT_NOT_NULL(handles_buf);

  fdf_handle_t* handles = reinterpret_cast<fdf_handle_t*>(handles_buf);
  handles[0] = closed_ch;

  EXPECT_EQ(ZX_ERR_INVALID_ARGS,
            fdf_channel_write(local_.get(), 0, arena_.get(), nullptr, 0, handles, 1));

  fdf_handle_close(additional_ch);
}

// Tests providing non arena-managed data in a channel message.
TEST_F(ChannelTest, WriteNonManagedData) {
  uint8_t data[100];
  ASSERT_EQ(ZX_ERR_INVALID_ARGS,
            fdf_channel_write(local_.get(), 0, arena_.get(), data, 100, NULL, 0));
}

// Tests providing a non arena-managed handles array in a channel message.
TEST_F(ChannelTest, WriteNonManagedHandles) {
  fdf_handle_t transfer_ch;
  fdf_handle_t additional_ch;
  ASSERT_EQ(ZX_OK, fdf_channel_create(0, &transfer_ch, &additional_ch));

  EXPECT_EQ(ZX_ERR_INVALID_ARGS,
            fdf_channel_write(local_.get(), 0, arena_.get(), nullptr, 0, &transfer_ch, 1));

  fdf_handle_close(transfer_ch);
  fdf_handle_close(additional_ch);
}

// Tests writing to the channel after the peer has closed their end.
TEST_F(ChannelTest, WriteClosedPeer) {
  local_.reset();

  void* data = arena_.Allocate(64);
  ASSERT_EQ(ZX_ERR_PEER_CLOSED,
            fdf_channel_write(remote_.get(), 0, arena_.get(), data, 64, NULL, 0));
}

TEST_F(ChannelTest, WriteSelfHandleReturnsNotSupported) {
  void* handles_buf = arena_.Allocate(sizeof(fdf_handle_t));
  ASSERT_NOT_NULL(handles_buf);

  fdf_handle_t* handles = reinterpret_cast<fdf_handle_t*>(handles_buf);
  handles[0] = local_.get();

  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED,
            fdf_channel_write(local_.get(), 0, arena_.get(), nullptr, 0, handles, 1));
}

TEST_F(ChannelTest, WriteWaitedHandle) {
  fdf_handle_t local;
  fdf_handle_t remote;
  ASSERT_EQ(ZX_OK, fdf_channel_create(0, &local, &remote));

  auto channel_read_ = std::make_unique<fdf::ChannelRead>(
      remote, 0 /* options */,
      [](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {});
  ASSERT_OK(channel_read_->Begin(fdf_dispatcher_));

  void* handles_buf = arena_.Allocate(sizeof(fdf_handle_t));
  ASSERT_NOT_NULL(handles_buf);

  fdf_handle_t* handles = reinterpret_cast<fdf_handle_t*>(handles_buf);
  handles[0] = remote;

  ASSERT_NOT_OK(fdf_channel_write(local_.get(), 0, arena_.get(), nullptr, 0, handles, 1));

  fdf_handle_close(local);
  fdf_handle_close(remote);
}

//
// Tests for fdf_channel_read error conditions
//

// Tests reading from a closed channel handle.
TEST_F(ChannelTest, ReadToClosedHandle) {
  local_.reset();

  ASSERT_DEATH([&] {
    EXPECT_EQ(ZX_ERR_BAD_HANDLE,
              fdf_channel_read(local_.get(), 0, nullptr, nullptr, 0, nullptr, 0));
  });
}

TEST_F(ChannelTest, ReadNullArenaWithData) {
  void* data = arena_.Allocate(64);
  ASSERT_EQ(ZX_OK, fdf_channel_write(local_.get(), 0, arena_.get(), data, 64, NULL, 0));
  ASSERT_NO_FATAL_FAILURE(WaitUntilReadReady(remote_.get()));
  void* out_data;
  uint32_t num_bytes;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS,
            fdf_channel_read(remote_.get(), 0, nullptr, &out_data, &num_bytes, nullptr, 0));
}

TEST_F(ChannelTest, ReadNullArenaWithHandles) {
  fdf_handle_t transfer_ch_local;
  fdf_handle_t transfer_ch_remote;
  ASSERT_EQ(ZX_OK, fdf_channel_create(0, &transfer_ch_local, &transfer_ch_remote));

  void* handles_buf = arena_.Allocate(sizeof(fdf_handle_t));
  ASSERT_NOT_NULL(handles_buf);

  fdf_handle_t* handles = reinterpret_cast<fdf_handle_t*>(handles_buf);
  handles[0] = transfer_ch_remote;

  ASSERT_EQ(ZX_OK, fdf_channel_write(local_.get(), 0, arena_.get(), nullptr, 0, handles, 1));
  ASSERT_NO_FATAL_FAILURE(WaitUntilReadReady(remote_.get()));
  fdf_handle_t* read_handles;
  uint32_t num_handles;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, fdf_channel_read(remote_.get(), 0, nullptr, nullptr, nullptr,
                                                  &read_handles, &num_handles));

  fdf_handle_close(transfer_ch_local);
  // We do not need to to close the transferred handle, as it is consumed by |fdf_channel_write|.
}

// Tests reading from the channel before any message has been sent.
TEST_F(ChannelTest, ReadWhenEmptyReturnsShouldWait) {
  fdf_arena_t* arena;
  void* data;
  uint32_t num_bytes;
  ASSERT_EQ(ZX_ERR_SHOULD_WAIT,
            fdf_channel_read(local_.get(), 0, &arena, &data, &num_bytes, nullptr, 0));
}

TEST_F(ChannelTest, ReadWhenEmptyAndClosedReturnsPeerClosed) {
  remote_.reset();

  fdf_arena_t* arena;
  void* data;
  uint32_t num_bytes;
  ASSERT_EQ(ZX_ERR_PEER_CLOSED,
            fdf_channel_read(local_.get(), 0, &arena, &data, &num_bytes, nullptr, 0));
}

// Tests reading from the channel after the peer has closed their end.
TEST_F(ChannelTest, ReadClosedPeer) {
  local_.reset();

  ASSERT_EQ(ZX_ERR_PEER_CLOSED,
            fdf_channel_read(remote_.get(), 0, nullptr, nullptr, 0, nullptr, 0));
}

//
// Tests for fdf_channel_wait_async error conditions
//

TEST_F(ChannelTest, WaitAsyncClosedPeerNoPendingMsgs) {
  local_.reset();

  auto channel_read_ = std::make_unique<fdf::ChannelRead>(
      remote_.get(), 0 /* options */,
      [](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {});
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, channel_read_->Begin(fdf_dispatcher_));
}

TEST_F(ChannelTest, WaitAsyncAlreadyWaiting) {
  auto channel_read_ = std::make_unique<fdf::ChannelRead>(
      local_.get(), 0 /* options */,
      [](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {});
  EXPECT_OK(channel_read_->Begin(fdf_dispatcher_));

  auto channel_read2_ = std::make_unique<fdf::ChannelRead>(
      local_.get(), 0 /* options */,
      [](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {});
  EXPECT_EQ(ZX_ERR_BAD_STATE, channel_read2_->Begin(fdf_dispatcher_));

  EXPECT_OK(fdf_channel_write(remote_.get(), 0, nullptr, nullptr, 0, nullptr, 0));

  ASSERT_NO_FATAL_FAILURE(WaitUntilReadReady(local_.get()));
}

//
// Tests for fdf_channel_call error conditions
//

TEST_F(ChannelTest, CallWrittenBytesSmallerThanFdfTxIdReturnsInvalidArgs) {
  constexpr uint32_t kDataSize = sizeof(fdf_txid_t) - 1;

  void* data = arena_.Allocate(kDataSize);
  auto read =
      local_.Call(0, zx::time::infinite(), arena_, data, kDataSize, cpp20::span<zx_handle_t>());
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, read.status_value());
}

TEST_F(ChannelTest, CallToClosedHandle) {
  constexpr uint32_t kDataSize = sizeof(fdf_txid_t);
  void* data = arena_.Allocate(kDataSize);

  local_.reset();

  ASSERT_DEATH([&] {
    auto read =
        local_.Call(0, zx::time::infinite(), arena_, data, kDataSize, cpp20::span<zx_handle_t>());
    ASSERT_EQ(ZX_ERR_BAD_HANDLE, read.status_value());
  });
}

// Tests providing a closed handle as part of a channel message.
TEST_F(ChannelTest, CallTransferClosedHandle) {
  constexpr uint32_t kDataSize = sizeof(fdf_txid_t);
  void* data = arena_.Allocate(kDataSize);

  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_OK(channels.status_value());

  void* handles_buf = arena_.Allocate(sizeof(fdf_handle_t));
  ASSERT_NOT_NULL(handles_buf);

  fdf_handle_t* handles = reinterpret_cast<fdf_handle_t*>(handles_buf);
  handles[0] = channels->end0.get();

  channels->end0.reset();

  auto read = local_.Call(0, zx::time::infinite(), arena_, data, kDataSize,
                          cpp20::span<zx_handle_t>(handles, 1));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, read.status_value());
}

// Tests providing non arena-managed data in a channel message.
TEST_F(ChannelTest, CallTransferNonManagedData) {
  constexpr uint32_t kDataSize = sizeof(fdf_txid_t);
  uint8_t data[kDataSize];
  auto read =
      local_.Call(0, zx::time::infinite(), arena_, data, kDataSize, cpp20::span<zx_handle_t>());
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, read.status_value());
}

// Tests providing a non arena-managed handles array in a channel message.
TEST_F(ChannelTest, CallTransferNonManagedHandles) {
  constexpr uint32_t kDataSize = sizeof(fdf_txid_t);
  void* data = arena_.Allocate(kDataSize);

  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_OK(channels.status_value());

  fdf_handle_t handle = channels->end0.get();

  auto read = local_.Call(0, zx::time::infinite(), arena_, data, kDataSize,
                          cpp20::span<zx_handle_t>(&handle, 1));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, read.status_value());
}

// Tests writing to the channel after the peer has closed their end.
TEST_F(ChannelTest, CallClosedPeer) {
  constexpr uint32_t kDataSize = sizeof(fdf_txid_t);
  void* data = arena_.Allocate(kDataSize);

  fdf_handle_close(remote_.release());

  auto read =
      local_.Call(0, zx::time::infinite(), arena_, data, kDataSize, cpp20::span<zx_handle_t>());
  ASSERT_EQ(ZX_ERR_PEER_CLOSED, read.status_value());
}

TEST_F(ChannelTest, CallTransferSelfHandleReturnsNotSupported) {
  constexpr uint32_t kDataSize = sizeof(fdf_txid_t);
  void* data = arena_.Allocate(kDataSize);

  void* handles_buf = arena_.Allocate(sizeof(fdf_handle_t));
  ASSERT_NOT_NULL(handles_buf);

  fdf_handle_t* handles = reinterpret_cast<fdf_handle_t*>(handles_buf);
  handles[0] = local_.get();

  auto read = local_.Call(0, zx::time::infinite(), arena_, data, kDataSize,
                          cpp20::span<zx_handle_t>(handles, 1));
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, read.status_value());
}

TEST_F(ChannelTest, CallTransferWaitedHandle) {
  constexpr uint32_t kDataSize = sizeof(fdf_txid_t);
  void* data = arena_.Allocate(kDataSize);

  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_OK(channels.status_value());

  auto channel_read_ = std::make_unique<fdf::ChannelRead>(
      channels->end0.get(), 0 /* options */,
      [](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        ASSERT_EQ(status, ZX_ERR_PEER_CLOSED);
        delete channel_read;
      });
  ASSERT_OK(channel_read_->Begin(fdf_dispatcher_));
  channel_read_.release();  // Deleted on callback.

  void* handles_buf = arena_.Allocate(sizeof(fdf_handle_t));
  ASSERT_NOT_NULL(handles_buf);

  fdf_handle_t* handles = reinterpret_cast<fdf_handle_t*>(handles_buf);
  handles[0] = channels->end0.get();

  auto read = local_.Call(0, zx::time::infinite(), arena_, data, kDataSize,
                          cpp20::span<zx_handle_t>(handles, 1));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, read.status_value());
}

TEST_F(ChannelTest, CallConsumesHandlesOnError) {
  constexpr uint32_t kDataSize = sizeof(fdf_txid_t);
  void* data = arena_.Allocate(kDataSize);

  constexpr uint32_t kNumHandles = 2;

  // Create some handles to transfer.
  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));

  zx::event event2;
  ASSERT_OK(zx::event::create(0, &event2));

  void* handles_buf = arena_.Allocate(kNumHandles * sizeof(fdf_handle_t));
  ASSERT_NOT_NULL(handles_buf);

  fdf_handle_t* handles = reinterpret_cast<fdf_handle_t*>(handles_buf);
  handles[0] = event.release();
  handles[1] = event2.release();

  // Close the remote end of the channel so the call will fail.
  remote_.reset();

  auto read = local_.Call(0, zx::time::infinite(), arena_, data, kDataSize,
                          cpp20::span<zx_handle_t>(handles, kNumHandles));
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, read.status_value());

  for (uint32_t i = 0; i < kNumHandles; i++) {
    ASSERT_EQ(ZX_ERR_BAD_HANDLE, zx_handle_close(handles[i]));
  }
}

TEST_F(ChannelTest, CallNotifiedOnPeerClosed) {
  constexpr uint32_t kDataSize = sizeof(fdf_txid_t);
  void* data = arena_.Allocate(kDataSize);

  {
    test_utils::AutoJoinThread service_thread(
        [&](fdf::Channel svc) {
          // Make the call non-reentrant.
          driver_context::PushDriver(CreateFakeDriver());

          // Wait until call message is received.
          ASSERT_NO_FATAL_FAILURE(WaitUntilReadReady(svc.get()));
          // Close the peer.
          svc.reset();
        },
        std::move(remote_));

    auto read =
        local_.Call(0, zx::time::infinite(), arena_, data, kDataSize, cpp20::span<zx_handle_t>());
    EXPECT_EQ(ZX_ERR_PEER_CLOSED, read.status_value());
  }
}

TEST_F(ChannelTest, CallManagedThreadDisallowsSyncCalls) {
  static constexpr uint32_t kNumBytes = 4;
  void* data = arena_.Allocate(kNumBytes);
  ASSERT_EQ(ZX_OK, fdf_channel_write(local_.get(), 0, arena_.get(), data, kNumBytes, NULL, 0));

  auto channel_read = std::make_unique<fdf::ChannelRead>(
      remote_.get(), 0,
      [&](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        fdf::UnownedChannel unowned(channel_read->channel());

        auto read = unowned->Read(0);
        ASSERT_OK(read.status_value());

        auto call = unowned->Call(0, zx::time::infinite(), arena_, data, kNumBytes,
                                  cpp20::span<zx_handle_t>());
        ASSERT_EQ(ZX_ERR_BAD_STATE, call.status_value());
      });
  ASSERT_OK(channel_read->Begin(fdf_dispatcher_));
}

//
// Tests for C++ API
//

TEST_F(ChannelTest, MoveConstructor) {
  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_EQ(ZX_OK, channels.status_value());
  local_ = std::move(channels->end0);
  remote_ = std::move(channels->end1);

  local_.reset();
  remote_.reset();

  ASSERT_EQ(0, driver_runtime::gHandleTableArena.num_allocated());
}

TEST(ChannelTest, IsValid) {
  fdf::Channel invalid_channel;
  ASSERT_FALSE(invalid_channel.is_valid());

  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_TRUE(channels->end0.is_valid());

  channels->end0.close();
  ASSERT_FALSE(channels->end0.is_valid());
}
