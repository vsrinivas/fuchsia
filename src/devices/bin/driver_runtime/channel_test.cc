// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_runtime/channel.h"

#include <lib/async-loop/cpp/loop.h>
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
  std::unique_ptr<driver_runtime::Dispatcher> dispatcher_;
  // Type casted version of |dispatcher_|.
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

  ASSERT_EQ(ZX_OK, driver_runtime::Dispatcher::CreateWithLoop(
                       FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, "scheduler_role", 0,
                       CreateFakeDriver(), &loop_, &dispatcher_));

  fdf_dispatcher_ = static_cast<fdf_dispatcher_t*>(dispatcher_.get());

  // Pretend all calls are non-reentrant so we don't have to worry about threading.
  driver_context::PushDriver(CreateFakeDriver());
}

void ChannelTest::TearDown() {
  local_.reset();
  remote_.reset();

  arena_.reset();

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
  ASSERT_NO_FATAL_FAILURES(WaitUntilReadReady(remote_.get()));
  ASSERT_NO_FATAL_FAILURES(AssertRead(remote_.get(), nullptr, 0, nullptr, 0));
}

// Tests writing and reading an array of numbers.
TEST_F(ChannelTest, WriteData) {
  constexpr uint32_t kNumBytes = 24 * 1024;

  void* data;
  AllocateTestData(arena_.get(), kNumBytes, &data);

  ASSERT_EQ(ZX_OK, fdf_channel_write(local_.get(), 0, arena_.get(), data, kNumBytes, NULL, 0));
  ASSERT_NO_FATAL_FAILURES(WaitUntilReadReady(remote_.get()));
  ASSERT_NO_FATAL_FAILURES(AssertRead(remote_.get(), data, kNumBytes, nullptr, 0));
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

  ASSERT_NO_FATAL_FAILURES(WaitUntilReadReady(remote_.get()));
  ASSERT_NO_FATAL_FAILURES(AssertRead(remote_.get(), nullptr, 0, handles, 1));
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
  ASSERT_NO_FATAL_FAILURES(WaitUntilReadReady(remote_.get()));
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

  ASSERT_NO_FATAL_FAILURES(WaitUntilReadReady(b0));
  ASSERT_NO_FATAL_FAILURES(AssertRead(b0, data, kNumBytes, nullptr, 0));

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

  ASSERT_NO_FATAL_FAILURES(AssertRead(remote_.get(), data, kNumBytes, nullptr, 0));
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
        ASSERT_NO_FATAL_FAILURES(AssertRead(remote_.get(), data, kFirstMsgNumBytes, nullptr, 0));
        ASSERT_NO_FATAL_FAILURES(AssertRead(remote_.get(), data2, kSecondMsgNumBytes, nullptr, 0));
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
        ASSERT_NO_FATAL_FAILURES(
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

  std::unique_ptr<driver_runtime::Dispatcher> async_dispatcher;
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
  ASSERT_OK(channel_read->Begin(static_cast<fdf_dispatcher_t*>(async_dispatcher.get())));

  remote_.reset();

  sync_completion_wait(&read_completion, ZX_TIME_INFINITE);

  loop_.Quit();
  loop_.JoinThreads();
}

TEST_F(ChannelTest, CancelSynchronousDispatcherCallbackOnClose) {
  const void* driver = CreateFakeDriver();
  std::unique_ptr<driver_runtime::Dispatcher> sync_dispatcher;
  ASSERT_EQ(ZX_OK,
            driver_runtime::Dispatcher::CreateWithLoop(0, "", 0, driver, &loop_, &sync_dispatcher));

  ASSERT_EQ(ZX_OK, fdf_channel_write(local_.get(), 0, arena_.get(), nullptr, 0, nullptr, 0));

  // Make the read reentrant so that the callback will be queued on the async loop.
  driver_context::PushDriver(driver);
  auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });

  // Since there is a pending message, this should queue a callback on the dispatcher.
  sync_completion_t read_completion;
  auto channel_read = std::make_unique<fdf::ChannelRead>(
      remote_.get(), 0,
      [&read_completion](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read,
                         fdf_status_t status) { sync_completion_signal(&read_completion); });
  ASSERT_OK(channel_read->Begin(static_cast<fdf_dispatcher_t*>(sync_dispatcher.get())));

  ASSERT_EQ(sync_dispatcher->callback_queue_size_slow(), 1);

  remote_.reset();

  ASSERT_EQ(sync_dispatcher->callback_queue_size_slow(), 0);
}

// Tests that you can wait on and read pending messages from a channel even if the peer is closed.
TEST_F(ChannelTest, ReadRemainingMessagesWhenPeerIsClosed) {
  void* data = arena_.Allocate(64);
  ASSERT_EQ(ZX_OK, fdf_channel_write(local_.get(), 0, arena_.get(), data, 64, NULL, 0));

  local_.reset();

  ASSERT_NO_FATAL_FAILURES(WaitUntilReadReady(remote_.get()));
  ASSERT_NO_FATAL_FAILURES(AssertRead(remote_.get(), data, 64, nullptr, 0));
}

// Tests that read provides ownership of an arena.
TEST_F(ChannelTest, ReadArenaOwnership) {
  void* data = arena_.Allocate(64);
  ASSERT_EQ(ZX_OK, fdf_channel_write(local_.get(), 0, arena_.get(), data, 64, NULL, 0));

  arena_.reset();

  fdf_arena_t* read_arena;
  ASSERT_NO_FATAL_FAILURES(WaitUntilReadReady(remote_.get()));
  ASSERT_NO_FATAL_FAILURES(AssertRead(remote_.get(), data, 64, nullptr, 0, &read_arena));

  // Re-use the arena provided by the read call.
  data = read_arena->Allocate(64);
  ASSERT_EQ(ZX_OK, fdf_channel_write(remote_.get(), 0, read_arena, data, 64, NULL, 0));

  fdf_arena_destroy(read_arena);

  ASSERT_NO_FATAL_FAILURES(WaitUntilReadReady(local_.get()));
  ASSERT_NO_FATAL_FAILURES(AssertRead(local_.get(), data, 64, nullptr, 0));
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
  ASSERT_NO_FATAL_FAILURES(WaitUntilReadReady(remote_.get()));
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
  ASSERT_NO_FATAL_FAILURES(WaitUntilReadReady(remote_.get()));
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

  ASSERT_NO_FATAL_FAILURES(WaitUntilReadReady(local_.get()));
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
