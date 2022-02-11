// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_runtime/dispatcher.h"

#include <lib/async/task.h>
#include <lib/fdf/arena.h>
#include <lib/fdf/channel.h>
#include <lib/fdf/cpp/channel_read.h>
#include <lib/fdf/dispatcher.h>
#include <lib/fit/defer.h>
#include <lib/sync/cpp/completion.h>

#include <thread>

#include <zxtest/zxtest.h>

#include "src/devices/bin/driver_runtime/driver_context.h"
#include "src/devices/bin/driver_runtime/runtime_test_case.h"

class DispatcherTest : public RuntimeTestCase {
 public:
  DispatcherTest() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  void SetUp() override;
  void TearDown() override;

  // Creates a dispatcher and returns it in |out_dispatcher|.
  // The dispatcher will automatically be destroyed in |TearDown|.
  void CreateDispatcher(uint32_t options, const char* scheduler_role, const void* owner,
                        fdf_dispatcher_t** out_dispatcher);

  // Registers an async read, which on callback will acquire |lock| and read from |read_channel|.
  // If |reply_channel| is not null, it will write an empty message.
  // If |completion| is not null, it will signal before returning from the callback.
  static void RegisterAsyncReadReply(fdf_handle_t read_channel, fdf_dispatcher_t* dispatcher,
                                     fbl::Mutex* lock,
                                     fdf_handle_t reply_channel = ZX_HANDLE_INVALID,
                                     sync_completion_t* completion = nullptr);

  // Registers an async read, which on callback will acquire |lock|, read from |read_channel| and
  // signal |completion|.
  static void RegisterAsyncReadSignal(fdf_handle_t read_channel, fdf_dispatcher_t* dispatcher,
                                      fbl::Mutex* lock, sync_completion_t* completion) {
    return RegisterAsyncReadReply(read_channel, dispatcher, lock, ZX_HANDLE_INVALID, completion);
  }

  // Registers an async read, which on callback will signal |entered_callback| and block
  // until |complete_blocking_read| is signaled.
  static void RegisterAsyncReadBlock(fdf_handle_t ch, fdf_dispatcher_t* dispatcher,
                                     sync_completion_t* entered_callback,
                                     sync_completion_t* complete_blocking_read);

  fdf_handle_t local_ch_;
  fdf_handle_t remote_ch_;

  fdf_handle_t local_ch2_;
  fdf_handle_t remote_ch2_;

  async::Loop loop_;
  std::vector<fdf_dispatcher_t*> dispatchers_;
};

void DispatcherTest::SetUp() {
  ASSERT_EQ(ZX_OK, fdf_channel_create(0, &local_ch_, &remote_ch_));
  ASSERT_EQ(ZX_OK, fdf_channel_create(0, &local_ch2_, &remote_ch2_));

  loop_.StartThread();
}

void DispatcherTest::TearDown() {
  loop_.Quit();
  loop_.JoinThreads();

  if (local_ch_) {
    fdf_handle_close(local_ch_);
  }
  if (remote_ch_) {
    fdf_handle_close(remote_ch_);
  }
  if (local_ch2_) {
    fdf_handle_close(local_ch2_);
  }
  if (remote_ch2_) {
    fdf_handle_close(remote_ch2_);
  }
  for (auto* dispatcher : dispatchers_) {
    fdf_dispatcher_destroy_async(dispatcher);
  }
}

void DispatcherTest::CreateDispatcher(uint32_t options, const char* scheduler_role,
                                      const void* owner, fdf_dispatcher_t** out_dispatcher) {
  driver_runtime::Dispatcher* dispatcher;
  ASSERT_EQ(ZX_OK,
            driver_runtime::Dispatcher::CreateWithLoop(
                options, scheduler_role, strlen(scheduler_role), owner, &loop_, &dispatcher));
  *out_dispatcher = static_cast<fdf_dispatcher_t*>(dispatcher);
  dispatchers_.push_back(*out_dispatcher);
}

// static
void DispatcherTest::RegisterAsyncReadReply(fdf_handle_t read_channel, fdf_dispatcher_t* dispatcher,
                                            fbl::Mutex* lock, fdf_handle_t reply_channel,
                                            sync_completion_t* completion) {
  auto channel_read = std::make_unique<fdf::ChannelRead>(
      read_channel, 0 /* options */,
      [=](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        ASSERT_OK(status);

        {
          fbl::AutoLock auto_lock(lock);

          ASSERT_NO_FATAL_FAILURE(AssertRead(channel_read->channel(), nullptr, 0, nullptr, 0));
          if (reply_channel != ZX_HANDLE_INVALID) {
            ASSERT_EQ(ZX_OK, fdf_channel_write(reply_channel, 0, nullptr, nullptr, 0, nullptr, 0));
          }
        }
        if (completion) {
          sync_completion_signal(completion);
        }
        delete channel_read;
      });
  ASSERT_OK(channel_read->Begin(dispatcher));
  channel_read.release();  // Deleted on callback.
}

// static
void DispatcherTest::RegisterAsyncReadBlock(fdf_handle_t ch, fdf_dispatcher_t* dispatcher,
                                            sync_completion_t* entered_callback,
                                            sync_completion_t* complete_blocking_read) {
  auto channel_read = std::make_unique<fdf::ChannelRead>(
      ch, 0 /* options */,
      [=](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        ASSERT_OK(status);
        sync_completion_signal(entered_callback);
        ASSERT_OK(sync_completion_wait(complete_blocking_read, ZX_TIME_INFINITE));
        delete channel_read;
      });
  ASSERT_OK(channel_read->Begin(dispatcher));
  channel_read.release();  // Will be deleted on callback.
}

//
// Synchronous dispatcher tests
//

// Tests that a synchronous dispatcher will call directly into the next driver
// if it is not reentrant.
// This creates 2 drivers and writes a message between them.
TEST_F(DispatcherTest, SyncDispatcherDirectCall) {
  const void* local_driver = CreateFakeDriver();
  const void* remote_driver = CreateFakeDriver();

  // We should bypass the async loop, so quit it now to make sure we don't use it.
  loop_.Quit();
  loop_.JoinThreads();

  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", local_driver, &dispatcher));

  sync_completion_t read_completion;
  ASSERT_NO_FATAL_FAILURE(SignalOnChannelReadable(local_ch_, dispatcher, &read_completion));

  {
    driver_context::PushDriver(remote_driver);
    auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });
    // As |local_driver| is not in the thread's call stack,
    // this should call directly into local driver's channel_read callback.
    ASSERT_EQ(ZX_OK, fdf_channel_write(remote_ch_, 0, nullptr, nullptr, 0, nullptr, 0));
    ASSERT_OK(sync_completion_wait(&read_completion, ZX_TIME_INFINITE));
  }
}

// Tests that a synchronous dispatcher will queue a request on the async loop if it is reentrant.
// This writes and reads a message from the same driver.
TEST_F(DispatcherTest, SyncDispatcherCallOnLoop) {
  const void* driver = CreateFakeDriver();

  loop_.Quit();
  loop_.JoinThreads();
  loop_.ResetQuit();

  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", driver, &dispatcher));

  sync_completion_t read_completion;
  ASSERT_NO_FATAL_FAILURE(SignalOnChannelReadable(local_ch_, dispatcher, &read_completion));

  {
    // Add the same driver to the thread's call stack.
    driver_context::PushDriver(driver);
    auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });

    // This should queue the callback to run on an async loop thread.
    ASSERT_EQ(ZX_OK, fdf_channel_write(remote_ch_, 0, nullptr, nullptr, 0, nullptr, 0));
    // Check that the callback hasn't been called yet, as we shutdown the async loop.
    ASSERT_FALSE(sync_completion_signaled(&read_completion));
    ASSERT_EQ(1, dispatcher->callback_queue_size_slow());
  }

  loop_.StartThread();
  ASSERT_OK(sync_completion_wait(&read_completion, ZX_TIME_INFINITE));
}

// Tests that a synchronous dispatcher only allows one callback to be running at a time.
// We will register a callback that blocks and one that doesnt. We will then send
// 2 requests, and check that the second callback is not run until the first returns.
TEST_F(DispatcherTest, SyncDispatcherDisallowsParallelCallbacks) {
  const void* driver = CreateFakeDriver();
  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", driver, &dispatcher));

  // We shouldn't actually block on a dispatcher that doesn't have ALLOW_SYNC_CALLS set,
  // but this is just for synchronizing the test.
  sync_completion_t entered_callback;
  sync_completion_t complete_blocking_read;
  ASSERT_NO_FATAL_FAILURE(
      RegisterAsyncReadBlock(local_ch_, dispatcher, &entered_callback, &complete_blocking_read));

  sync_completion_t read_completion;
  ASSERT_NO_FATAL_FAILURE(SignalOnChannelReadable(local_ch2_, dispatcher, &read_completion));

  {
    // This should make the callback run on the async loop, as it would be reentrant.
    driver_context::PushDriver(driver);
    auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });
    ASSERT_EQ(ZX_OK, fdf_channel_write(remote_ch_, 0, nullptr, nullptr, 0, nullptr, 0));
  }

  ASSERT_OK(sync_completion_wait(&entered_callback, ZX_TIME_INFINITE));

  // Write another request. This should also be queued on the async loop.
  std::thread t1 = std::thread([&] {
    // Make the call not reentrant.
    driver_context::PushDriver(CreateFakeDriver());
    auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });
    ASSERT_EQ(ZX_OK, fdf_channel_write(remote_ch2_, 0, nullptr, nullptr, 0, nullptr, 0));
  });

  // The dispatcher should not call the callback while there is an existing callback running,
  // so we should be able to join with the thread immediately.
  t1.join();
  ASSERT_FALSE(sync_completion_signaled(&read_completion));

  // Complete the first callback.
  sync_completion_signal(&complete_blocking_read);

  // The second callback should complete now.
  ASSERT_OK(sync_completion_wait(&read_completion, ZX_TIME_INFINITE));
}

// Tests that a synchronous dispatcher does not schedule parallel callbacks on the async loop.
TEST_F(DispatcherTest, SyncDispatcherDisallowsParallelCallbacksReentrant) {
  loop_.Quit();
  loop_.JoinThreads();
  loop_.ResetQuit();

  constexpr uint32_t kNumThreads = 2;
  constexpr uint32_t kNumClients = 12;

  const void* driver = CreateFakeDriver();
  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", driver, &dispatcher));

  struct ReadClient {
    fdf_handle_t channel;
    sync_completion_t entered_callback;
    sync_completion_t complete_blocking_read;
  };

  std::vector<ReadClient> local(kNumClients);
  std::vector<fdf_handle_t> remote(kNumClients);

  for (uint32_t i = 0; i < kNumClients; i++) {
    ASSERT_OK(fdf_channel_create(0, &local[i].channel, &remote[i]));
    ASSERT_NO_FATAL_FAILURE(RegisterAsyncReadBlock(local[i].channel, dispatcher,
                                                   &local[i].entered_callback,
                                                   &local[i].complete_blocking_read));
  }

  for (uint32_t i = 0; i < kNumClients; i++) {
    // Call is considered reentrant and will be queued on the async loop.
    ASSERT_EQ(ZX_OK, fdf_channel_write(remote[i], 0, nullptr, nullptr, 0, nullptr, 0));
  }

  for (uint32_t i = 0; i < kNumThreads; i++) {
    loop_.StartThread();
  }

  ASSERT_OK(sync_completion_wait(&local[0].entered_callback, ZX_TIME_INFINITE));
  sync_completion_signal(&local[0].complete_blocking_read);

  // Check that we aren't blocking the second thread by posting a task to another
  // dispatcher.
  fdf_dispatcher_t* dispatcher2;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", driver, &dispatcher2));
  async_dispatcher_t* async_dispatcher = fdf_dispatcher_get_async_dispatcher(dispatcher2);
  ASSERT_NOT_NULL(async_dispatcher);

  sync_completion_t task_completion;
  ASSERT_OK(async::PostTask(async_dispatcher,
                            [&task_completion] { sync_completion_signal(&task_completion); }));
  ASSERT_OK(loop_.RunUntilIdle());

  ASSERT_OK(sync_completion_wait(&task_completion, ZX_TIME_INFINITE));

  // Allow all the read callbacks to complete.
  for (uint32_t i = 1; i < kNumClients; i++) {
    sync_completion_signal(&local[i].complete_blocking_read);
  }

  for (uint32_t i = 0; i < kNumClients; i++) {
    ASSERT_OK(sync_completion_wait(&local[i].entered_callback, ZX_TIME_INFINITE));
  }

  loop_.Quit();
  loop_.JoinThreads();

  for (uint32_t i = 0; i < kNumClients; i++) {
    fdf_handle_close(local[i].channel);
    fdf_handle_close(remote[i]);
  }
}

//
// Unsynchronized dispatcher tests
//

// Tests that an unsynchronized dispatcher allows multiple callbacks to run at the same time.
// We will send requests from multiple threads and check that the expected number of callbacks
// is running.
TEST_F(DispatcherTest, UnsyncDispatcherAllowsParallelCallbacks) {
  const void* driver = CreateFakeDriver();
  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, "scheduler_role",
                                           driver, &dispatcher));

  constexpr uint32_t kNumClients = 10;

  std::vector<fdf_handle_t> local(kNumClients);
  std::vector<fdf_handle_t> remote(kNumClients);

  for (uint32_t i = 0; i < kNumClients; i++) {
    ASSERT_OK(fdf_channel_create(0, &local[i], &remote[i]));
  }

  fbl::Mutex callback_lock;
  uint32_t num_callbacks = 0;
  sync_completion_t completion;

  for (uint32_t i = 0; i < kNumClients; i++) {
    auto channel_read = std::make_unique<fdf::ChannelRead>(
        local[i], 0 /* options */,
        [&](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
          {
            fbl::AutoLock lock(&callback_lock);
            num_callbacks++;
            if (num_callbacks == kNumClients) {
              sync_completion_signal(&completion);
            }
          }
          // Wait for all threads to ensure we are correctly supporting parallel callbacks.
          ASSERT_OK(sync_completion_wait(&completion, ZX_TIME_INFINITE));
          delete channel_read;
        });
    ASSERT_OK(channel_read->Begin(dispatcher));
    channel_read.release();  // Deleted by the callback.
  }

  std::vector<std::thread> threads;
  for (uint32_t i = 0; i < kNumClients; i++) {
    std::thread client = std::thread(
        [&](fdf_handle_t channel) {
          {
            // Ensure the call is not reentrant.
            driver_context::PushDriver(CreateFakeDriver());
            auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });
            ASSERT_EQ(ZX_OK, fdf_channel_write(channel, 0, nullptr, nullptr, 0, nullptr, 0));
          }
        },
        remote[i]);
    threads.push_back(std::move(client));
  }

  for (auto& t : threads) {
    t.join();
  }

  for (uint32_t i = 0; i < kNumClients; i++) {
    fdf_handle_close(local[i]);
    fdf_handle_close(remote[i]);
  }
}

// Tests that an unsynchronized dispatcher allows multiple callbacks to run at the same time
// on the async loop.
TEST_F(DispatcherTest, UnsyncDispatcherAllowsParallelCallbacksReentrant) {
  loop_.Quit();
  loop_.JoinThreads();
  loop_.ResetQuit();

  constexpr uint32_t kNumThreads = 3;
  constexpr uint32_t kNumClients = 22;

  const void* driver = CreateFakeDriver();
  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, "scheduler_role",
                                           driver, &dispatcher));

  std::vector<fdf_handle_t> local(kNumClients);
  std::vector<fdf_handle_t> remote(kNumClients);

  for (uint32_t i = 0; i < kNumClients; i++) {
    ASSERT_OK(fdf_channel_create(0, &local[i], &remote[i]));
  }

  fbl::Mutex callback_lock;
  uint32_t num_callbacks = 0;
  sync_completion_t all_threads_running;

  for (uint32_t i = 0; i < kNumClients; i++) {
    auto channel_read = std::make_unique<fdf::ChannelRead>(
        local[i], 0 /* options */,
        [&](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
          {
            fbl::AutoLock lock(&callback_lock);
            num_callbacks++;
            if (num_callbacks == kNumThreads) {
              sync_completion_signal(&all_threads_running);
            }
          }
          // Wait for all threads to ensure we are correctly supporting parallel callbacks.
          ASSERT_OK(sync_completion_wait(&all_threads_running, ZX_TIME_INFINITE));
          delete channel_read;
        });
    ASSERT_OK(channel_read->Begin(dispatcher));
    channel_read.release();  // Deleted by the callback.
  }

  for (uint32_t i = 0; i < kNumClients; i++) {
    // Call is considered reentrant and will be queued on the async loop.
    ASSERT_EQ(ZX_OK, fdf_channel_write(remote[i], 0, nullptr, nullptr, 0, nullptr, 0));
  }

  for (uint32_t i = 0; i < kNumThreads; i++) {
    loop_.StartThread();
  }

  ASSERT_OK(sync_completion_wait(&all_threads_running, ZX_TIME_INFINITE));
  loop_.Quit();
  loop_.JoinThreads();
  ASSERT_EQ(num_callbacks, kNumClients);

  for (uint32_t i = 0; i < kNumClients; i++) {
    fdf_handle_close(local[i]);
    fdf_handle_close(remote[i]);
  }
}

//
// Blocking dispatcher tests
//

// Tests that a blocking dispatcher will not directly call into the next driver.
TEST_F(DispatcherTest, AllowSyncCallsDoesNotDirectlyCall) {
  const void* blocking_driver = CreateFakeDriver();
  fdf_dispatcher_t* blocking_dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS, "scheduler_role",
                                           blocking_driver, &blocking_dispatcher));

  // Queue a blocking request.
  sync_completion_t entered_callback;
  sync_completion_t complete_blocking_read;
  ASSERT_NO_FATAL_FAILURE(RegisterAsyncReadBlock(remote_ch_, blocking_dispatcher, &entered_callback,
                                                 &complete_blocking_read));

  {
    // Simulate a driver writing a message to the driver with the blocking dispatcher.
    driver_context::PushDriver(CreateFakeDriver());
    auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });

    // This is a non reentrant call, but we still shouldn't call into the driver directly.
    ASSERT_EQ(ZX_OK, fdf_channel_write(local_ch_, 0, nullptr, nullptr, 0, nullptr, 0));
  }

  ASSERT_OK(sync_completion_wait(&entered_callback, ZX_TIME_INFINITE));

  // Signal and wait for the blocking read handler to return.
  sync_completion_signal(&complete_blocking_read);
  loop_.Quit();
  loop_.JoinThreads();
}

// Tests that a blocking dispatcher does not block the global async loop shared between
// all dispatchers in a process.
// We will register a blocking callback, and ensure we can receive other callbacks
// at the same time.
TEST_F(DispatcherTest, AllowSyncCallsDoesNotBlockGlobalLoop) {
  const void* driver = CreateFakeDriver();
  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", driver, &dispatcher));

  const void* blocking_driver = CreateFakeDriver();
  fdf_dispatcher_t* blocking_dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS, "scheduler_role",
                                           blocking_driver, &blocking_dispatcher));

  fdf_handle_t blocking_local_ch, blocking_remote_ch;
  ASSERT_EQ(ZX_OK, fdf_channel_create(0, &blocking_local_ch, &blocking_remote_ch));

  // Queue a blocking read.
  sync_completion_t entered_callback;
  sync_completion_t complete_blocking_read;
  ASSERT_NO_FATAL_FAILURE(RegisterAsyncReadBlock(blocking_remote_ch, blocking_dispatcher,
                                                 &entered_callback, &complete_blocking_read));

  // Write a message for the blocking dispatcher.
  {
    driver_context::PushDriver(blocking_driver);
    auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });
    ASSERT_EQ(ZX_OK, fdf_channel_write(blocking_local_ch, 0, nullptr, nullptr, 0, nullptr, 0));
  }

  ASSERT_OK(sync_completion_wait(&entered_callback, ZX_TIME_INFINITE));

  sync_completion_t read_completion;
  ASSERT_NO_FATAL_FAILURE(SignalOnChannelReadable(remote_ch_, dispatcher, &read_completion));

  {
    // Write a message which will be read on the non-blocking dispatcher.
    // Make the call reentrant so that the request is queued for the async loop.
    driver_context::PushDriver(driver);
    auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });
    ASSERT_EQ(ZX_OK, fdf_channel_write(local_ch_, 0, nullptr, nullptr, 0, nullptr, 0));
  }

  ASSERT_OK(sync_completion_wait(&read_completion, ZX_TIME_INFINITE));
  ASSERT_NO_FATAL_FAILURE(AssertRead(remote_ch_, nullptr, 0, nullptr, 0));

  // Signal and wait for the blocking read handler to return.
  sync_completion_signal(&complete_blocking_read);
  loop_.Quit();
  loop_.JoinThreads();

  fdf_handle_close(blocking_local_ch);
  fdf_handle_close(blocking_remote_ch);
}

//
// Additional re-entrancy tests
//

// Tests sending a request to another driver and receiving a reply across a single channel.
TEST_F(DispatcherTest, ReentrancySimpleSendAndReply) {
  // Create a dispatcher for each end of the channel.
  const void* driver = CreateFakeDriver();
  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", driver, &dispatcher));

  const void* driver2 = CreateFakeDriver();
  fdf_dispatcher_t* dispatcher2;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", driver2, &dispatcher2));

  // Lock that is acquired by the first driver whenever it writes or reads from |local_ch_|.
  // We shouldn't need to lock in a synchronous dispatcher, but this is just for testing
  // that the dispatcher handles reentrant calls. If the dispatcher attempts to call
  // reentrantly, this test will deadlock.
  fbl::Mutex driver_lock;
  fbl::Mutex driver2_lock;
  sync_completion_t completion;

  ASSERT_NO_FATAL_FAILURE(
      RegisterAsyncReadSignal(local_ch_, dispatcher, &driver_lock, &completion));
  ASSERT_NO_FATAL_FAILURE(
      RegisterAsyncReadReply(remote_ch_, dispatcher2, &driver2_lock, remote_ch_));

  {
    driver_context::PushDriver(driver);
    auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });

    fbl::AutoLock lock(&driver_lock);
    // This should call directly into the next driver. When the driver writes its reply,
    // the dispatcher should detect that it is reentrant and queue it to be run on the
    // async loop. This will allow |fdf_channel_write| to return  and |driver_lock| will
    // be released.
    ASSERT_EQ(ZX_OK, fdf_channel_write(local_ch_, 0, nullptr, nullptr, 0, nullptr, 0));
  }

  ASSERT_OK(sync_completion_wait(&completion, ZX_TIME_INFINITE));
}

// Tests sending a request to another driver, who sends a request back into the original driver
// on a different channel.
TEST_F(DispatcherTest, ReentrancyMultipleDriversAndDispatchers) {
  // Driver will own |local_ch_| and |local_ch2_|.
  const void* driver = CreateFakeDriver();
  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", driver, &dispatcher));

  // Driver2 will own |remote_ch_| and |remote_ch2_|.
  const void* driver2 = CreateFakeDriver();
  fdf_dispatcher_t* dispatcher2;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", driver2, &dispatcher2));

  // Lock that is acquired by the driver whenever it writes or reads from its channels.
  // We shouldn't need to lock in a synchronous dispatcher, but this is just for testing
  // that the dispatcher handles reentrant calls. If the dispatcher attempts to call
  // reentrantly, this test will deadlock.
  fbl::Mutex driver_lock;
  fbl::Mutex driver2_lock;
  sync_completion_t completion;

  ASSERT_NO_FATAL_FAILURE(
      RegisterAsyncReadSignal(local_ch2_, dispatcher, &driver_lock, &completion));
  ASSERT_NO_FATAL_FAILURE(
      RegisterAsyncReadReply(remote_ch_, dispatcher2, &driver2_lock, remote_ch2_));

  {
    driver_context::PushDriver(driver);
    auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });

    fbl::AutoLock lock(&driver_lock);
    // This should call directly into the next driver. When the driver writes its reply,
    // the dispatcher should detect that it is reentrant and queue it to be run on the
    // async loop. This will allow |fdf_channel_write| to return and |driver_lock| will
    // be released.
    ASSERT_EQ(ZX_OK, fdf_channel_write(local_ch_, 0, nullptr, nullptr, 0, nullptr, 0));
  }

  ASSERT_OK(sync_completion_wait(&completion, ZX_TIME_INFINITE));
}

// Tests a driver sending a request to another channel it owns.
TEST_F(DispatcherTest, ReentrancyOneDriverMultipleChannels) {
  const void* driver = CreateFakeDriver();
  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", driver, &dispatcher));

  // Lock that is acquired by the driver whenever it writes or reads from its channels.
  // We shouldn't need to lock in a synchronous dispatcher, but this is just for testing
  // that the dispatcher handles reentrant calls. If the dispatcher attempts to call
  // reentrantly, this test will deadlock.
  fbl::Mutex driver_lock;
  sync_completion_t completion;

  ASSERT_NO_FATAL_FAILURE(
      RegisterAsyncReadSignal(local_ch2_, dispatcher, &driver_lock, &completion));
  ASSERT_NO_FATAL_FAILURE(
      RegisterAsyncReadReply(remote_ch_, dispatcher, &driver_lock, remote_ch2_));

  {
    driver_context::PushDriver(driver);
    auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });

    fbl::AutoLock lock(&driver_lock);
    // Every call callback in this driver will be reentrant and should be run on the async loop.
    ASSERT_EQ(ZX_OK, fdf_channel_write(local_ch_, 0, nullptr, nullptr, 0, nullptr, 0));
  }

  ASSERT_OK(sync_completion_wait(&completion, ZX_TIME_INFINITE));
}

// Tests forwarding a request across many drivers, before calling back into the original driver.
TEST_F(DispatcherTest, ReentrancyManyDrivers) {
  constexpr uint32_t kNumDrivers = 30;

  // Each driver i uses ch_to_prev[i] and ch_to_next[i] to communicate with the driver before and
  // after it, except ch_to_prev[0] and ch_to_next[kNumDrivers-1].
  std::vector<fdf_handle_t> ch_to_prev(kNumDrivers);
  std::vector<fdf_handle_t> ch_to_next(kNumDrivers);

  // Lock that is acquired by the driver whenever it writes or reads from its channels.
  // We shouldn't need to lock in a synchronous dispatcher, but this is just for testing
  // that the dispatcher handles reentrant calls. If the dispatcher attempts to call
  // reentrantly, this test will deadlock.
  std::vector<fbl::Mutex> driver_locks(kNumDrivers);

  for (uint32_t i = 0; i < kNumDrivers; i++) {
    const void* driver = CreateFakeDriver();
    fdf_dispatcher_t* dispatcher;
    ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", driver, &dispatcher));

    // Get the next driver's channel which is connected to the current driver's channel.
    // The last driver will be connected to the first driver.
    fdf_handle_t* peer = (i == kNumDrivers - 1) ? &ch_to_prev[0] : &ch_to_prev[i + 1];
    ASSERT_OK(fdf_channel_create(0, &ch_to_next[i], peer));
  }

  // Signal once the first driver is called into.
  sync_completion_t completion;
  ASSERT_NO_FATAL_FAILURE(RegisterAsyncReadSignal(ch_to_prev[0],
                                                  static_cast<fdf_dispatcher_t*>(dispatchers_[0]),
                                                  &driver_locks[0], &completion));

  // Each driver will wait for a callback, then write a message to the next driver.
  for (uint32_t i = 1; i < kNumDrivers; i++) {
    ASSERT_NO_FATAL_FAILURE(RegisterAsyncReadReply(ch_to_prev[i],
                                                   static_cast<fdf_dispatcher_t*>(dispatchers_[i]),
                                                   &driver_locks[i], ch_to_next[i]));
  }

  {
    driver_context::PushDriver(dispatchers_[0]->owner());
    auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });

    fbl::AutoLock lock(&driver_locks[0]);
    // Write from the first driver.
    // This should call directly into the next |kNumDrivers - 1| drivers.
    ASSERT_EQ(ZX_OK, fdf_channel_write(ch_to_next[0], 0, nullptr, nullptr, 0, nullptr, 0));
  }

  ASSERT_OK(sync_completion_wait(&completion, ZX_TIME_INFINITE));

  for (uint32_t i = 0; i < kNumDrivers; i++) {
    fdf_handle_close(ch_to_prev[i]);
    fdf_handle_close(ch_to_next[i]);
  }
}

// Tests writing a request from an unknown driver context.
TEST_F(DispatcherTest, EmptyCallStack) {
  loop_.Quit();
  loop_.JoinThreads();
  loop_.ResetQuit();

  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", CreateFakeDriver(), &dispatcher));

  sync_completion_t read_completion;
  ASSERT_NO_FATAL_FAILURE(SignalOnChannelReadable(local_ch_, dispatcher, &read_completion));

  {
    // Call without any recorded call stack.
    // This should queue the callback to run on an async loop thread.
    ASSERT_EQ(ZX_OK, fdf_channel_write(remote_ch_, 0, nullptr, nullptr, 0, nullptr, 0));
    ASSERT_EQ(1, dispatcher->callback_queue_size_slow());
    ASSERT_FALSE(sync_completion_signaled(&read_completion));
  }

  loop_.StartThread();
  ASSERT_OK(sync_completion_wait(&read_completion, ZX_TIME_INFINITE));
}

TEST_F(DispatcherTest, DestroyDispatcherInAsyncLoopCallback) {
  const void* driver = CreateFakeDriver();
  auto scheduler_role = "scheduler_role";

  driver_runtime::Dispatcher* dispatcher;
  ASSERT_EQ(ZX_OK, driver_runtime::Dispatcher::CreateWithLoop(
                       FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, scheduler_role, strlen(scheduler_role),
                       driver, &loop_, &dispatcher));

  sync::Completion completion;
  auto channel_read = std::make_unique<fdf::ChannelRead>(
      remote_ch_, 0 /* options */,
      [&](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        ASSERT_OK(status);
        fdf_dispatcher_destroy_async(dispatcher);
        completion.Signal();
        delete channel_read;
      });
  ASSERT_OK(channel_read->Begin(static_cast<fdf_dispatcher_t*>(dispatcher)));
  channel_read.release();  // Deleted on callback.

  {
    // Make the write reentrant so it is scheduled to run on the async loop.
    driver_context::PushDriver(driver);
    auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });

    ASSERT_EQ(ZX_OK, fdf_channel_write(local_ch_, 0, nullptr, nullptr, 0, nullptr, 0));
  }

  ASSERT_OK(completion.Wait(zx::time::infinite()));

  loop_.Quit();
  loop_.JoinThreads();
}

//
// async_dispatcher_t
//

// Tests that we can use the fdf_dispatcher_t as an async_dispatcher_t.
TEST_F(DispatcherTest, AsyncDispatcher) {
  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", CreateFakeDriver(), &dispatcher));

  async_dispatcher_t* async_dispatcher = fdf_dispatcher_get_async_dispatcher(dispatcher);
  ASSERT_NOT_NULL(async_dispatcher);

  sync_completion_t completion;
  ASSERT_OK(
      async::PostTask(async_dispatcher, [&completion] { sync_completion_signal(&completion); }));
  ASSERT_OK(loop_.RunUntilIdle());

  ASSERT_OK(sync_completion_wait(&completion, ZX_TIME_INFINITE));
}

TEST_F(DispatcherTest, FromAsyncDispatcher) {
  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", CreateFakeDriver(), &dispatcher));

  async_dispatcher_t* async_dispatcher = fdf_dispatcher_get_async_dispatcher(dispatcher);
  ASSERT_NOT_NULL(async_dispatcher);

  ASSERT_EQ(fdf_dispatcher_from_async_dispatcher(async_dispatcher), dispatcher);
}

TEST_F(DispatcherTest, CancelTask) {
  loop_.Quit();
  loop_.JoinThreads();
  loop_.ResetQuit();

  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", CreateFakeDriver(), &dispatcher));

  async_dispatcher_t* async_dispatcher = fdf_dispatcher_get_async_dispatcher(dispatcher);
  ASSERT_NOT_NULL(async_dispatcher);

  async::TaskClosure task;
  task.set_handler([] { ASSERT_FALSE(true); });
  ASSERT_OK(task.Post(async_dispatcher));

  ASSERT_OK(task.Cancel());  // Task should not be running yet.
}

TEST_F(DispatcherTest, CancelTaskNotYetPosted) {
  loop_.Quit();
  loop_.JoinThreads();
  loop_.ResetQuit();

  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", CreateFakeDriver(), &dispatcher));

  async_dispatcher_t* async_dispatcher = fdf_dispatcher_get_async_dispatcher(dispatcher);
  ASSERT_NOT_NULL(async_dispatcher);

  async::TaskClosure task;
  task.set_handler([] { ASSERT_FALSE(true); });

  ASSERT_EQ(task.Cancel(), ZX_ERR_NOT_FOUND);  // Task should not be running yet.
}

TEST_F(DispatcherTest, CancelTaskAlreadyRunning) {
  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", CreateFakeDriver(), &dispatcher));

  async_dispatcher_t* async_dispatcher = fdf_dispatcher_get_async_dispatcher(dispatcher);
  ASSERT_NOT_NULL(async_dispatcher);

  async::TaskClosure task;
  sync::Completion completion;
  task.set_handler([&] {
    ASSERT_EQ(task.Cancel(), ZX_ERR_NOT_FOUND);  // Task is already running.
    completion.Signal();
  });
  ASSERT_OK(task.Post(async_dispatcher));
  ASSERT_OK(completion.Wait(zx::time::infinite()));
}

//
// Error handling
//

// Tests that you cannot create an unsynchronized blocking dispatcher.
TEST_F(DispatcherTest, CreateUnsynchronizedAllowSyncCallsFails) {
  driver_context::PushDriver(CreateFakeDriver());
  auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });

  driver_runtime::Dispatcher* dispatcher;
  uint32_t options = FDF_DISPATCHER_OPTION_UNSYNCHRONIZED | FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS;
  ASSERT_NE(ZX_OK, fdf_dispatcher::Create(options, "scheduler_role", 0, &dispatcher));
}

// Tests that you cannot create a dispatcher on a thread not managed by the driver runtime.
TEST_F(DispatcherTest, CreateDispatcherOnNonRuntimeThreadFails) {
  driver_runtime::Dispatcher* dispatcher;
  ASSERT_NE(ZX_OK, fdf_dispatcher::Create(0, "scheduler_role", 0, &dispatcher));
}
