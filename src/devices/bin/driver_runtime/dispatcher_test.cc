// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_runtime/dispatcher.h"

#include <lib/async/cpp/wait.h>
#include <lib/async/task.h>
#include <lib/fdf/arena.h>
#include <lib/fdf/channel.h>
#include <lib/fdf/cpp/channel_read.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fit/defer.h>
#include <lib/sync/cpp/completion.h>
#include <lib/zx/event.h>
#include <zircon/errors.h>

#include <thread>

#include <zxtest/zxtest.h>

#include "lib/fdf/dispatcher.h"
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
                                     libsync::Completion* entered_callback,
                                     libsync::Completion* complete_blocking_read);

  fdf_handle_t local_ch_;
  fdf_handle_t remote_ch_;

  fdf_handle_t local_ch2_;
  fdf_handle_t remote_ch2_;

  async::Loop loop_;
  std::vector<fdf_dispatcher_t*> dispatchers_;
  std::vector<std::unique_ptr<DispatcherShutdownObserver>> observers_;
};

void DispatcherTest::SetUp() {
  ASSERT_EQ(ZX_OK, fdf_channel_create(0, &local_ch_, &remote_ch_));
  ASSERT_EQ(ZX_OK, fdf_channel_create(0, &local_ch2_, &remote_ch2_));

  loop_.StartThread();
}

void DispatcherTest::TearDown() {
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

  loop_.StartThread();  // Make sure an async loop thread is running for dispatcher destruction.

  for (auto* dispatcher : dispatchers_) {
    fdf_dispatcher_shutdown_async(dispatcher);
  }
  for (auto& observer : observers_) {
    ASSERT_OK(observer->WaitUntilShutdown());
  }
  for (auto* dispatcher : dispatchers_) {
    fdf_dispatcher_destroy(dispatcher);
  }

  loop_.Quit();
  loop_.JoinThreads();
}

void DispatcherTest::CreateDispatcher(uint32_t options, const char* scheduler_role,
                                      const void* owner, fdf_dispatcher_t** out_dispatcher) {
  auto observer = std::make_unique<DispatcherShutdownObserver>();
  driver_runtime::Dispatcher* dispatcher;
  ASSERT_EQ(ZX_OK, driver_runtime::Dispatcher::CreateWithLoop(
                       options, scheduler_role, strlen(scheduler_role), owner, &loop_,
                       observer->fdf_observer(), &dispatcher));
  *out_dispatcher = static_cast<fdf_dispatcher_t*>(dispatcher);
  dispatchers_.push_back(*out_dispatcher);
  observers_.push_back(std::move(observer));
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
                                            libsync::Completion* entered_callback,
                                            libsync::Completion* complete_blocking_read) {
  auto channel_read = std::make_unique<fdf::ChannelRead>(
      ch, 0 /* options */,
      [=](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        ASSERT_OK(status);
        entered_callback->Signal();
        ASSERT_OK(complete_blocking_read->Wait(zx::time::infinite()));
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
  loop_.ResetQuit();

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
  libsync::Completion entered_callback;
  libsync::Completion complete_blocking_read;
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

  ASSERT_OK(entered_callback.Wait(zx::time::infinite()));

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
  complete_blocking_read.Signal();

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
    libsync::Completion entered_callback;
    libsync::Completion complete_blocking_read;
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

  ASSERT_OK(local[0].entered_callback.Wait(zx::time::infinite()));
  local[0].complete_blocking_read.Signal();

  // Check that we aren't blocking the second thread by posting a task to another
  // dispatcher.
  fdf_dispatcher_t* dispatcher2;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", driver, &dispatcher2));
  async_dispatcher_t* async_dispatcher = fdf_dispatcher_get_async_dispatcher(dispatcher2);
  ASSERT_NOT_NULL(async_dispatcher);

  sync_completion_t task_completion;
  ASSERT_OK(async::PostTask(async_dispatcher,
                            [&task_completion] { sync_completion_signal(&task_completion); }));
  ASSERT_OK(sync_completion_wait(&task_completion, ZX_TIME_INFINITE));

  // Allow all the read callbacks to complete.
  for (uint32_t i = 1; i < kNumClients; i++) {
    local[i].complete_blocking_read.Signal();
  }

  for (uint32_t i = 0; i < kNumClients; i++) {
    ASSERT_OK(local[i].entered_callback.Wait(zx::time::infinite()));
  }

  ASSERT_OK(fdf_internal_wait_until_dispatcher_idle(dispatcher));
  ASSERT_OK(fdf_internal_wait_until_dispatcher_idle(dispatcher2));

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
  ASSERT_OK(fdf_internal_wait_until_dispatcher_idle(dispatcher));
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
  libsync::Completion entered_callback;
  libsync::Completion complete_blocking_read;
  ASSERT_NO_FATAL_FAILURE(RegisterAsyncReadBlock(remote_ch_, blocking_dispatcher, &entered_callback,
                                                 &complete_blocking_read));

  {
    // Simulate a driver writing a message to the driver with the blocking dispatcher.
    driver_context::PushDriver(CreateFakeDriver());
    auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });

    // This is a non reentrant call, but we still shouldn't call into the driver directly.
    ASSERT_EQ(ZX_OK, fdf_channel_write(local_ch_, 0, nullptr, nullptr, 0, nullptr, 0));
  }

  ASSERT_OK(entered_callback.Wait(zx::time::infinite()));

  // Signal and wait for the blocking read handler to return.
  complete_blocking_read.Signal();

  ASSERT_OK(fdf_internal_wait_until_dispatcher_idle(blocking_dispatcher));
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
  libsync::Completion entered_callback;
  libsync::Completion complete_blocking_read;
  ASSERT_NO_FATAL_FAILURE(RegisterAsyncReadBlock(blocking_remote_ch, blocking_dispatcher,
                                                 &entered_callback, &complete_blocking_read));

  // Write a message for the blocking dispatcher.
  {
    driver_context::PushDriver(blocking_driver);
    auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });
    ASSERT_EQ(ZX_OK, fdf_channel_write(blocking_local_ch, 0, nullptr, nullptr, 0, nullptr, 0));
  }

  ASSERT_OK(entered_callback.Wait(zx::time::infinite()));

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
  complete_blocking_read.Signal();

  ASSERT_OK(fdf_internal_wait_until_dispatcher_idle(dispatcher));
  ASSERT_OK(fdf_internal_wait_until_dispatcher_idle(blocking_dispatcher));

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
    // async loop. This will allow |fdf_channel_write| to return and |driver_lock| will
    // be released.
    ASSERT_EQ(ZX_OK, fdf_channel_write(local_ch_, 0, nullptr, nullptr, 0, nullptr, 0));
  }

  ASSERT_OK(sync_completion_wait(&completion, ZX_TIME_INFINITE));

  //  ASSERT_OK(fdf_internal_wait_until_dispatcher_idle(dispatcher));
  //  ASSERT_OK(fdf_internal_wait_until_dispatcher_idle(dispatcher2));
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
    ASSERT_OK(fdf_internal_wait_until_dispatcher_idle(dispatchers_[i]));
  }
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

//
// Shutdown() tests
//

// Tests shutting down a synchronized dispatcher that has a pending channel read
// that does not have a corresponding channel write.
TEST_F(DispatcherTest, SyncDispatcherShutdownBeforeWrite) {
  libsync::Completion read_complete;
  DispatcherShutdownObserver observer;

  const void* driver = CreateFakeDriver();
  auto scheduler_role = "scheduler_role";

  driver_runtime::Dispatcher* dispatcher;
  ASSERT_EQ(ZX_OK, driver_runtime::Dispatcher::CreateWithLoop(
                       0, scheduler_role, strlen(scheduler_role), driver, &loop_,
                       observer.fdf_observer(), &dispatcher));

  fdf::Dispatcher fdf_dispatcher(static_cast<fdf_dispatcher_t*>(dispatcher));

  // Registered, but not yet ready to run.
  auto channel_read = std::make_unique<fdf::ChannelRead>(
      remote_ch_, 0,
      [&](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        ASSERT_EQ(status, ZX_ERR_CANCELED);
        read_complete.Signal();
        delete channel_read;
      });
  ASSERT_OK(channel_read->Begin(fdf_dispatcher.get()));
  channel_read.release();

  fdf_dispatcher.ShutdownAsync();

  ASSERT_OK(read_complete.Wait(zx::time::infinite()));
  ASSERT_OK(observer.WaitUntilShutdown());
}

// Tests shutting down a synchronized dispatcher that has a pending async wait
// that hasn't been signaled yet.
TEST_F(DispatcherTest, SyncDispatcherShutdownBeforeSignaled) {
  libsync::Completion wait_complete;
  DispatcherShutdownObserver observer;

  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));

  async::WaitOnce wait(event.get(), ZX_USER_SIGNAL_0);

  const void* driver = CreateFakeDriver();
  auto scheduler_role = "scheduler_role";

  driver_runtime::Dispatcher* dispatcher;
  ASSERT_EQ(ZX_OK, driver_runtime::Dispatcher::CreateWithLoop(
                       0, scheduler_role, strlen(scheduler_role), driver, &loop_,
                       observer.fdf_observer(), &dispatcher));

  fdf::Dispatcher fdf_dispatcher(static_cast<fdf_dispatcher_t*>(dispatcher));

  // Registered, but not yet signaled.
  async_dispatcher_t* async_dispatcher = dispatcher->GetAsyncDispatcher();
  ASSERT_NOT_NULL(async_dispatcher);

  ASSERT_OK(wait.Begin(async_dispatcher, [&wait_complete, event = std::move(event)](
                                             async_dispatcher_t* dispatcher, async::WaitOnce* wait,
                                             zx_status_t status, const zx_packet_signal_t* signal) {
    ASSERT_STATUS(status, ZX_ERR_CANCELED);
    wait_complete.Signal();
  }));

  // Shutdown the dispatcher, which should schedule cancellation of the channel read.
  dispatcher->ShutdownAsync();

  ASSERT_OK(wait_complete.Wait(zx::time::infinite()));
  ASSERT_OK(observer.WaitUntilShutdown());
}

// Tests shutting down an unsynchronized dispatcher.
TEST_F(DispatcherTest, UnsyncDispatcherShutdown) {
  libsync::Completion complete_task;
  libsync::Completion read_complete;

  DispatcherShutdownObserver observer;

  const void* driver = CreateFakeDriver();
  auto scheduler_role = "scheduler_role";

  driver_runtime::Dispatcher* dispatcher;
  ASSERT_EQ(ZX_OK, driver_runtime::Dispatcher::CreateWithLoop(
                       FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, scheduler_role, strlen(scheduler_role),
                       driver, &loop_, observer.fdf_observer(), &dispatcher));

  fdf::Dispatcher fdf_dispatcher(static_cast<fdf_dispatcher_t*>(dispatcher));
  libsync::Completion task_started;
  // Post a task that will block until we signal it.
  ASSERT_OK(async::PostTask(fdf_dispatcher.async_dispatcher(), [&] {
    task_started.Signal();
    ASSERT_OK(complete_task.Wait(zx::time::infinite()));
  }));
  // Ensure the task has been started.
  ASSERT_OK(task_started.Wait(zx::time::infinite()));

  // Register a channel read, which should not be queued until the
  // write happens.
  auto channel_read = std::make_unique<fdf::ChannelRead>(
      remote_ch_, 0,
      [&](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        ASSERT_EQ(status, ZX_ERR_CANCELED);
        read_complete.Signal();
        delete channel_read;
      });
  ASSERT_OK(channel_read->Begin(fdf_dispatcher.get()));
  channel_read.release();

  {
    driver_context::PushDriver(driver);
    auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });
    // This should be considered reentrant and be queued on the async loop.
    ASSERT_EQ(ZX_OK, fdf_channel_write(local_ch_, 0, nullptr, nullptr, 0, nullptr, 0));
  }

  fdf_dispatcher.ShutdownAsync();

  // The cancellation should not happen until the task completes.
  ASSERT_FALSE(read_complete.signaled());
  complete_task.Signal();
  ASSERT_OK(read_complete.Wait(zx::time::infinite()));

  ASSERT_OK(observer.WaitUntilShutdown());
}

// Tests shutting down an unsynchronized dispatcher that has a pending channel read
// that does not have a corresponding channel write.
TEST_F(DispatcherTest, UnsyncDispatcherShutdonwBeforeWrite) {
  libsync::Completion read_complete;
  DispatcherShutdownObserver observer;

  const void* driver = CreateFakeDriver();
  auto scheduler_role = "scheduler_role";

  driver_runtime::Dispatcher* dispatcher;
  ASSERT_EQ(ZX_OK, driver_runtime::Dispatcher::CreateWithLoop(
                       FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, scheduler_role, strlen(scheduler_role),
                       driver, &loop_, observer.fdf_observer(), &dispatcher));

  fdf::Dispatcher fdf_dispatcher(static_cast<fdf_dispatcher_t*>(dispatcher));

  // Registered, but not yet ready to run.
  auto channel_read = std::make_unique<fdf::ChannelRead>(
      remote_ch_, 0,
      [&](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        ASSERT_EQ(status, ZX_ERR_CANCELED);
        read_complete.Signal();
        delete channel_read;
      });
  ASSERT_OK(channel_read->Begin(fdf_dispatcher.get()));
  channel_read.release();

  fdf_dispatcher.ShutdownAsync();

  ASSERT_OK(read_complete.Wait(zx::time::infinite()));
  ASSERT_OK(observer.WaitUntilShutdown());
}

// Tests shutting down a unsynchronized dispatcher that has a pending async wait
// that hasn't been signaled yet.
TEST_F(DispatcherTest, UnsyncDispatcherShutdownBeforeSignaled) {
  libsync::Completion wait_complete;
  DispatcherShutdownObserver observer;

  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));

  async::WaitOnce wait(event.get(), ZX_USER_SIGNAL_0);

  const void* driver = CreateFakeDriver();
  auto scheduler_role = "scheduler_role";

  driver_runtime::Dispatcher* dispatcher;
  ASSERT_EQ(ZX_OK, driver_runtime::Dispatcher::CreateWithLoop(
                       FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, scheduler_role, strlen(scheduler_role),
                       driver, &loop_, observer.fdf_observer(), &dispatcher));

  fdf::Dispatcher fdf_dispatcher(static_cast<fdf_dispatcher_t*>(dispatcher));

  // Registered, but not yet signaled.
  async_dispatcher_t* async_dispatcher = dispatcher->GetAsyncDispatcher();
  ASSERT_NOT_NULL(async_dispatcher);

  ASSERT_OK(wait.Begin(async_dispatcher, [&wait_complete, event = std::move(event)](
                                             async_dispatcher_t* dispatcher, async::WaitOnce* wait,
                                             zx_status_t status, const zx_packet_signal_t* signal) {
    ASSERT_STATUS(status, ZX_ERR_CANCELED);
    wait_complete.Signal();
  }));

  // Shutdown the dispatcher, which should schedule cancellation of the channel read.
  dispatcher->ShutdownAsync();

  ASSERT_OK(wait_complete.Wait(zx::time::infinite()));
  ASSERT_OK(observer.WaitUntilShutdown());
}

// Tests shutting down an unsynchronized dispatcher from a channel read callback running
// on the async loop.
TEST_F(DispatcherTest, ShutdownDispatcherInAsyncLoopCallback) {
  const void* driver = CreateFakeDriver();
  auto scheduler_role = "scheduler_role";

  DispatcherShutdownObserver dispatcher_observer;

  driver_runtime::Dispatcher* dispatcher;
  ASSERT_EQ(ZX_OK, driver_runtime::Dispatcher::CreateWithLoop(
                       FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, scheduler_role, strlen(scheduler_role),
                       driver, &loop_, dispatcher_observer.fdf_observer(), &dispatcher));

  libsync::Completion completion;
  auto channel_read = std::make_unique<fdf::ChannelRead>(
      remote_ch_, 0 /* options */,
      [&](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        ASSERT_OK(status);
        fdf_dispatcher_shutdown_async(dispatcher);
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

  ASSERT_OK(dispatcher_observer.WaitUntilShutdown());
  dispatcher->Destroy();
}

// Tests that attempting to shut down a dispatcher twice from callbacks does not crash.
TEST_F(DispatcherTest, ShutdownDispatcherFromTwoCallbacks) {
  // Stop the async loop, so that the channel reads don't get scheduled
  // until after we shut down the dispatcher.
  loop_.Quit();
  loop_.JoinThreads();
  loop_.ResetQuit();

  DispatcherShutdownObserver observer;
  const void* driver = CreateFakeDriver();
  auto scheduler_role = "scheduler_role";

  driver_runtime::Dispatcher* dispatcher;
  ASSERT_EQ(ZX_OK, driver_runtime::Dispatcher::CreateWithLoop(
                       FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, scheduler_role, strlen(scheduler_role),
                       driver, &loop_, observer.fdf_observer(), &dispatcher));

  libsync::Completion completion;
  auto channel_read = std::make_unique<fdf::ChannelRead>(
      remote_ch_, 0 /* options */,
      [&](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        ASSERT_OK(status);
        fdf_dispatcher_shutdown_async(dispatcher);
        completion.Signal();
      });
  ASSERT_OK(channel_read->Begin(static_cast<fdf_dispatcher_t*>(dispatcher)));

  libsync::Completion completion2;
  auto channel_read2 = std::make_unique<fdf::ChannelRead>(
      remote_ch2_, 0 /* options */,
      [&](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        ASSERT_OK(status);
        fdf_dispatcher_shutdown_async(dispatcher);
        completion2.Signal();
      });
  ASSERT_OK(channel_read2->Begin(static_cast<fdf_dispatcher_t*>(dispatcher)));

  {
    // Make the writes reentrant so they are scheduled to run on the async loop.
    driver_context::PushDriver(driver);
    auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });

    ASSERT_EQ(ZX_OK, fdf_channel_write(local_ch_, 0, nullptr, nullptr, 0, nullptr, 0));
    ASSERT_EQ(ZX_OK, fdf_channel_write(local_ch2_, 0, nullptr, nullptr, 0, nullptr, 0));
  }

  loop_.StartThread();

  ASSERT_OK(completion.Wait(zx::time::infinite()));
  ASSERT_OK(completion2.Wait(zx::time::infinite()));
  ASSERT_OK(observer.WaitUntilShutdown());
  dispatcher->Destroy();
}

// Tests that queueing a ChannelRead while the dispatcher is shutting down fails.
TEST_F(DispatcherTest, ShutdownDispatcherQueueChannelReadCallback) {
  // Stop the async loop, so that the channel read doesn't get scheduled
  // until after we shut down the dispatcher.
  loop_.Quit();
  loop_.JoinThreads();
  loop_.ResetQuit();

  libsync::Completion read_complete;
  DispatcherShutdownObserver observer;

  const void* driver = CreateFakeDriver();
  auto scheduler_role = "scheduler_role";

  driver_runtime::Dispatcher* dispatcher;
  ASSERT_EQ(ZX_OK, driver_runtime::Dispatcher::CreateWithLoop(
                       FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, scheduler_role, strlen(scheduler_role),
                       driver, &loop_, observer.fdf_observer(), &dispatcher));

  fdf::Dispatcher fdf_dispatcher(static_cast<fdf_dispatcher_t*>(dispatcher));

  auto channel_read = std::make_unique<fdf::ChannelRead>(
      remote_ch_, 0,
      [&](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        ASSERT_EQ(status, ZX_ERR_CANCELED);
        // We should not be able to queue the read again.
        ASSERT_EQ(channel_read->Begin(dispatcher), ZX_ERR_UNAVAILABLE);
        read_complete.Signal();
        delete channel_read;
      });
  ASSERT_OK(channel_read->Begin(fdf_dispatcher.get()));
  channel_read.release();  // Deleted on callback.

  {
    driver_context::PushDriver(driver);
    auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });
    // This should be considered reentrant and be queued on the async loop.
    ASSERT_EQ(ZX_OK, fdf_channel_write(local_ch_, 0, nullptr, nullptr, 0, nullptr, 0));
  }

  fdf_dispatcher.ShutdownAsync();

  loop_.StartThread();

  ASSERT_OK(read_complete.Wait(zx::time::infinite()));
  ASSERT_OK(observer.WaitUntilShutdown());
}

TEST_F(DispatcherTest, ShutdownCallbackIsNotReentrant) {
  fbl::Mutex driver_lock;

  libsync::Completion completion;
  auto destructed_handler = [&](fdf_dispatcher_t* dispatcher) {
    { fbl::AutoLock lock(&driver_lock); }
    completion.Signal();
  };

  driver_context::PushDriver(CreateFakeDriver());
  auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });

  auto dispatcher = fdf::Dispatcher::Create(0, destructed_handler);
  ASSERT_FALSE(dispatcher.is_error());

  {
    fbl::AutoLock lock(&driver_lock);
    dispatcher->ShutdownAsync();
  }

  ASSERT_OK(completion.Wait(zx::time::infinite()));
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
  libsync::Completion completion;
  task.set_handler([&] {
    ASSERT_EQ(task.Cancel(), ZX_ERR_NOT_FOUND);  // Task is already running.
    completion.Signal();
  });
  ASSERT_OK(task.Post(async_dispatcher));
  ASSERT_OK(completion.Wait(zx::time::infinite()));
}

TEST_F(DispatcherTest, AsyncWaitOnce) {
  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", CreateFakeDriver(), &dispatcher));

  async_dispatcher_t* async_dispatcher = fdf_dispatcher_get_async_dispatcher(dispatcher);
  ASSERT_NOT_NULL(async_dispatcher);

  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));

  sync_completion_t completion;
  async::WaitOnce wait(event.get(), ZX_USER_SIGNAL_0);
  ASSERT_OK(wait.Begin(async_dispatcher, [&completion, &async_dispatcher](
                                             async_dispatcher_t* dispatcher, async::WaitOnce* wait,
                                             zx_status_t status, const zx_packet_signal_t* signal) {
    ASSERT_EQ(async_dispatcher, dispatcher);
    ASSERT_OK(status);
    sync_completion_signal(&completion);
  }));
  ASSERT_OK(event.signal(0, ZX_USER_SIGNAL_0));
  ASSERT_OK(sync_completion_wait(&completion, ZX_TIME_INFINITE));
}

TEST_F(DispatcherTest, CancelWait) {
  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", CreateFakeDriver(), &dispatcher));

  async_dispatcher_t* async_dispatcher = fdf_dispatcher_get_async_dispatcher(dispatcher);
  ASSERT_NOT_NULL(async_dispatcher);

  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));

  async::WaitOnce wait(event.get(), ZX_USER_SIGNAL_0);
  ASSERT_OK(wait.Begin(async_dispatcher,
                       [](async_dispatcher_t* dispatcher, async::WaitOnce* wait, zx_status_t status,
                          const zx_packet_signal_t* signal) { ZX_ASSERT(false); }));
  ASSERT_OK(wait.Cancel());
}

TEST_F(DispatcherTest, GetCurrentDispatcherInWait) {
  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", CreateFakeDriver(), &dispatcher));

  async_dispatcher_t* async_dispatcher = fdf_dispatcher_get_async_dispatcher(dispatcher);
  ASSERT_NOT_NULL(async_dispatcher);

  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));

  sync_completion_t completion;
  async::WaitOnce wait(event.get(), ZX_USER_SIGNAL_0);
  ASSERT_OK(wait.Begin(
      async_dispatcher,
      [&completion, &dispatcher](async_dispatcher_t* async_dispatcher, async::WaitOnce* wait,
                                 zx_status_t status, const zx_packet_signal_t* signal) {
        ASSERT_EQ(fdf_dispatcher_get_current_dispatcher(), dispatcher);
        ASSERT_OK(status);
        sync_completion_signal(&completion);
      }));
  ASSERT_OK(event.signal(0, ZX_USER_SIGNAL_0));
  ASSERT_OK(sync_completion_wait(&completion, ZX_TIME_INFINITE));
}

TEST_F(DispatcherTest, WaitSynchronized) {
  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", CreateFakeDriver(), &dispatcher));

  // Create a second dispatcher which allows sync calls to force multiple threads.
  fdf_dispatcher_t* unused_dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS, "scheduler_role",
                                           CreateFakeDriver(), &unused_dispatcher));

  async_dispatcher_t* async_dispatcher = fdf_dispatcher_get_async_dispatcher(dispatcher);
  ASSERT_NOT_NULL(async_dispatcher);

  zx::event event1, event2;
  ASSERT_OK(zx::event::create(0, &event1));
  ASSERT_OK(zx::event::create(0, &event2));

  fbl::Mutex lock1, lock2;
  sync_completion_t completion1, completion2;

  async::WaitOnce wait1(event1.get(), ZX_USER_SIGNAL_0);
  ASSERT_OK(wait1.Begin(
      async_dispatcher,
      [&completion1, &lock1, &lock2](async_dispatcher_t* dispatcher, async::WaitOnce* wait,
                                     zx_status_t status, const zx_packet_signal_t* signal) {
        // Take note of the order the locks are acquired here.
        {
          fbl::AutoLock al1(&lock1);
          fbl::AutoLock al2(&lock2);
        }
        sync_completion_signal(&completion1);
      }));
  async::WaitOnce wait2(event1.get(), ZX_USER_SIGNAL_0);
  ASSERT_OK(wait2.Begin(
      async_dispatcher,
      [&completion2, &lock1, &lock2](async_dispatcher_t* dispatcher, async::WaitOnce* wait,
                                     zx_status_t status, const zx_packet_signal_t* signal) {
        // Locks acquired here in opposite order. If these calls are ever made in parallel, then we
        // run into a deadlock. The test should hang and eventually timeout in that case.
        {
          fbl::AutoLock al2(&lock2);
          fbl::AutoLock al1(&lock1);
        }
        sync_completion_signal(&completion2);
      }));

  // While the order of these signals are serialized, the order in which the signals are observed by
  // the waits is not. As a result either of the above waits may trigger first.
  ASSERT_OK(event1.signal(0, ZX_USER_SIGNAL_0));
  ASSERT_OK(event2.signal(0, ZX_USER_SIGNAL_0));
  // The order of observing these completions does not matter.
  ASSERT_OK(sync_completion_wait(&completion2, ZX_TIME_INFINITE));
  ASSERT_OK(sync_completion_wait(&completion1, ZX_TIME_INFINITE));
}

//
// WaitUntilIdle tests
//

TEST_F(DispatcherTest, WaitUntilIdle) {
  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", CreateFakeDriver(), &dispatcher));

  ASSERT_TRUE(dispatcher->IsIdle());
  ASSERT_OK(fdf_internal_wait_until_dispatcher_idle(dispatcher));
  ASSERT_TRUE(dispatcher->IsIdle());
}

TEST_F(DispatcherTest, WaitUntilIdleWithDirectCall) {
  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", CreateFakeDriver(), &dispatcher));

  // We shouldn't actually block on a dispatcher that doesn't have ALLOW_SYNC_CALLS set,
  // but this is just for synchronizing the test.
  libsync::Completion entered_callback;
  libsync::Completion complete_blocking_read;
  ASSERT_NO_FATAL_FAILURE(
      RegisterAsyncReadBlock(local_ch_, dispatcher, &entered_callback, &complete_blocking_read));

  std::thread t1 = std::thread([&] {
    // Make the call not reentrant, so that the read will run immediately once the write happens.
    driver_context::PushDriver(CreateFakeDriver());
    auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });
    ASSERT_EQ(ZX_OK, fdf_channel_write(remote_ch_, 0, nullptr, nullptr, 0, nullptr, 0));
  });

  // Wait for the read callback to be called, it will block until we signal it to complete.
  ASSERT_OK(entered_callback.Wait(zx::time::infinite()));

  ASSERT_FALSE(dispatcher->IsIdle());

  // Start a thread that blocks until the dispatcher is idle.
  libsync::Completion wait_started;
  libsync::Completion wait_complete;
  std::thread t2 = std::thread([&] {
    wait_started.Signal();
    ASSERT_OK(fdf_internal_wait_until_dispatcher_idle(dispatcher));
    ASSERT_TRUE(dispatcher->IsIdle());
    wait_complete.Signal();
  });

  ASSERT_OK(wait_started.Wait(zx::time::infinite()));
  ASSERT_FALSE(wait_complete.signaled());
  ASSERT_FALSE(dispatcher->IsIdle());

  complete_blocking_read.Signal();

  // Dispatcher should be idle now.
  ASSERT_OK(wait_complete.Wait(zx::time::infinite()));

  t1.join();
  t2.join();
}

TEST_F(DispatcherTest, WaitUntilIdleWithAsyncLoop) {
  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", CreateFakeDriver(), &dispatcher));

  // We shouldn't actually block on a dispatcher that doesn't have ALLOW_SYNC_CALLS set,
  // but this is just for synchronizing the test.
  libsync::Completion entered_callback;
  libsync::Completion complete_blocking_read;
  ASSERT_NO_FATAL_FAILURE(
      RegisterAsyncReadBlock(local_ch_, dispatcher, &entered_callback, &complete_blocking_read));

  // Call is reentrant, so the read will be queued on the async loop.
  ASSERT_EQ(ZX_OK, fdf_channel_write(remote_ch_, 0, nullptr, nullptr, 0, nullptr, 0));
  ASSERT_FALSE(dispatcher->IsIdle());

  // Wait for the read callback to be called, it will block until we signal it to complete.
  ASSERT_OK(entered_callback.Wait(zx::time::infinite()));

  ASSERT_FALSE(dispatcher->IsIdle());

  complete_blocking_read.Signal();
  ASSERT_OK(fdf_internal_wait_until_dispatcher_idle(dispatcher));
  ASSERT_TRUE(dispatcher->IsIdle());
}

TEST_F(DispatcherTest, WaitUntilIdleCanceledRead) {
  loop_.Quit();
  loop_.JoinThreads();
  loop_.ResetQuit();

  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", CreateFakeDriver(), &dispatcher));

  auto channel_read = std::make_unique<fdf::ChannelRead>(
      local_ch_, 0,
      [&](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        ASSERT_FALSE(true);  // This callback should never be called.
      });
  ASSERT_OK(channel_read->Begin(dispatcher));

  // Call is reentrant, so the read will be queued on the async loop.
  ASSERT_EQ(ZX_OK, fdf_channel_write(remote_ch_, 0, nullptr, nullptr, 0, nullptr, 0));
  ASSERT_FALSE(dispatcher->IsIdle());

  ASSERT_OK(channel_read->Cancel());

  loop_.StartThread();

  ASSERT_OK(fdf_internal_wait_until_dispatcher_idle(dispatcher));
}

TEST_F(DispatcherTest, WaitUntilIdleCanceledWait) {
  loop_.Quit();
  loop_.JoinThreads();
  loop_.ResetQuit();

  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", CreateFakeDriver(), &dispatcher));

  async_dispatcher_t* async_dispatcher = fdf_dispatcher_get_async_dispatcher(dispatcher);
  ASSERT_NOT_NULL(async_dispatcher);

  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));

  async::WaitOnce wait(event.get(), ZX_USER_SIGNAL_0);
  ASSERT_OK(
      wait.Begin(async_dispatcher,
                 [](async_dispatcher_t* async_dispatcher, async::WaitOnce* wait, zx_status_t status,
                    const zx_packet_signal_t* signal) { ASSERT_FALSE(true); }));
  ASSERT_OK(event.signal(0, ZX_USER_SIGNAL_0));
  ASSERT_FALSE(dispatcher->IsIdle());

  ASSERT_OK(wait.Cancel());

  loop_.StartThread();

  ASSERT_OK(fdf_internal_wait_until_dispatcher_idle(dispatcher));
}

TEST_F(DispatcherTest, WaitUntilIdleWithAsyncLoopMultipleThreads) {
  loop_.Quit();
  loop_.JoinThreads();
  loop_.ResetQuit();

  constexpr uint32_t kNumThreads = 2;
  constexpr uint32_t kNumClients = 22;

  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, "scheduler_role",
                                           CreateFakeDriver(), &dispatcher));

  struct ReadClient {
    fdf::Channel channel;
    libsync::Completion entered_callback;
    libsync::Completion complete_blocking_read;
  };

  std::vector<ReadClient> local(kNumClients);
  std::vector<fdf::Channel> remote(kNumClients);

  for (uint32_t i = 0; i < kNumClients; i++) {
    auto channels = fdf::ChannelPair::Create(0);
    ASSERT_OK(channels.status_value());
    local[i].channel = std::move(channels->end0);
    remote[i] = std::move(channels->end1);
    ASSERT_NO_FATAL_FAILURE(RegisterAsyncReadBlock(local[i].channel.get(), dispatcher,
                                                   &local[i].entered_callback,
                                                   &local[i].complete_blocking_read));
  }

  fdf::Arena arena;
  for (uint32_t i = 0; i < kNumClients; i++) {
    // Call is considered reentrant and will be queued on the async loop.
    auto write_status = remote[i].Write(0, arena, nullptr, 0, cpp20::span<zx_handle_t>());
    ASSERT_OK(write_status.status_value());
  }

  for (uint32_t i = 0; i < kNumThreads; i++) {
    loop_.StartThread();
  }

  ASSERT_OK(local[0].entered_callback.Wait(zx::time::infinite()));
  local[0].complete_blocking_read.Signal();

  ASSERT_FALSE(dispatcher->IsIdle());

  // Allow all the read callbacks to complete.
  for (uint32_t i = 1; i < kNumClients; i++) {
    local[i].complete_blocking_read.Signal();
  }

  ASSERT_OK(fdf_internal_wait_until_dispatcher_idle(dispatcher));

  for (uint32_t i = 0; i < kNumClients; i++) {
    ASSERT_TRUE(local[i].complete_blocking_read.signaled());
  }
}

TEST_F(DispatcherTest, WaitUntilIdleMultipleDispatchers) {
  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", CreateFakeDriver(), &dispatcher));

  fdf_dispatcher_t* dispatcher2;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", CreateFakeDriver(), &dispatcher2));

  // We shouldn't actually block on a dispatcher that doesn't have ALLOW_SYNC_CALLS set,
  // but this is just for synchronizing the test.
  libsync::Completion entered_callback;
  libsync::Completion complete_blocking_read;
  ASSERT_NO_FATAL_FAILURE(
      RegisterAsyncReadBlock(local_ch_, dispatcher, &entered_callback, &complete_blocking_read));

  // Call is reentrant, so the read will be queued on the async loop.
  ASSERT_EQ(ZX_OK, fdf_channel_write(remote_ch_, 0, nullptr, nullptr, 0, nullptr, 0));
  ASSERT_FALSE(dispatcher->IsIdle());

  // Wait for the read callback to be called, it will block until we signal it to complete.
  ASSERT_OK(entered_callback.Wait(zx::time::infinite()));

  ASSERT_FALSE(dispatcher->IsIdle());
  ASSERT_TRUE(dispatcher2->IsIdle());
  ASSERT_OK(fdf_internal_wait_until_dispatcher_idle(dispatcher2));

  complete_blocking_read.Signal();
  ASSERT_OK(fdf_internal_wait_until_dispatcher_idle(dispatcher));
  ASSERT_TRUE(dispatcher->IsIdle());
}

// Tests shutting down the process async loop while requests are still pending.
TEST_F(DispatcherTest, ShutdownProcessAsyncLoop) {
  DispatcherShutdownObserver observer;

  const void* driver = CreateFakeDriver();
  auto scheduler_role = "scheduler_role";

  driver_runtime::Dispatcher* dispatcher;
  ASSERT_EQ(ZX_OK, driver_runtime::Dispatcher::CreateWithLoop(
                       FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, scheduler_role, strlen(scheduler_role),
                       driver, &loop_, observer.fdf_observer(), &dispatcher));

  libsync::Completion entered_read;
  auto channel_read = std::make_unique<fdf::ChannelRead>(
      local_ch_, 0,
      [&](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        entered_read.Signal();
        // Do not let the read callback complete until the loop has entered a shutdown state.
        while (loop_.GetState() != ASYNC_LOOP_SHUTDOWN) {
        }
      });
  ASSERT_OK(channel_read->Begin(static_cast<fdf_dispatcher_t*>(dispatcher)));

  // Call is reentrant, so the read will be queued on the async loop.
  ASSERT_EQ(ZX_OK, fdf_channel_write(remote_ch_, 0, nullptr, nullptr, 0, nullptr, 0));
  // This will queue the wait to run |Dispatcher::CompleteShutdown|.
  dispatcher->ShutdownAsync();

  ASSERT_OK(entered_read.Wait(zx::time::infinite()));

  loop_.Shutdown();

  ASSERT_OK(observer.WaitUntilShutdown());
  dispatcher->Destroy();
}

TEST_F(DispatcherTest, SyncDispatcherCancelRequestDuringShutdown) {
  DispatcherShutdownObserver observer;

  const void* driver = CreateFakeDriver();
  auto scheduler_role = "scheduler_role";

  driver_runtime::Dispatcher* dispatcher;
  ASSERT_EQ(ZX_OK, driver_runtime::Dispatcher::CreateWithLoop(
                       0, scheduler_role, strlen(scheduler_role), driver, &loop_,
                       observer.fdf_observer(), &dispatcher));

  // Register a channel read that will be canceled by a posted task.
  auto channel_read = std::make_unique<fdf::ChannelRead>(
      local_ch_, 0,
      [&](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        ASSERT_FALSE(true);  // This should never be called.
      });
  ASSERT_OK(channel_read->Begin(static_cast<fdf_dispatcher_t*>(dispatcher)));

  libsync::Completion task_started;
  libsync::Completion dispatcher_shutdown_started;

  ASSERT_OK(async::PostTask(dispatcher->GetAsyncDispatcher(), [&] {
    task_started.Signal();
    ASSERT_OK(dispatcher_shutdown_started.Wait(zx::time::infinite()));
    ASSERT_OK(channel_read->Cancel());
  }));

  ASSERT_OK(task_started.Wait(zx::time::infinite()));

  // |Dispatcher::ShutdownAsync| will move the registered channel read into |shutdown_queue_|.
  dispatcher->ShutdownAsync();
  dispatcher_shutdown_started.Signal();

  ASSERT_OK(observer.WaitUntilShutdown());
  dispatcher->Destroy();
}

//
// Misc tests
//

TEST_F(DispatcherTest, GetCurrentDispatcherNone) {
  ASSERT_NULL(fdf_dispatcher_get_current_dispatcher());
}

TEST_F(DispatcherTest, GetCurrentDispatcher) {
  const void* driver1 = CreateFakeDriver();
  fdf_dispatcher_t* dispatcher1;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", driver1, &dispatcher1));

  const void* driver2 = CreateFakeDriver();
  fdf_dispatcher_t* dispatcher2;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", driver2, &dispatcher2));

  // driver1 will wait on a message from driver2, then reply back.
  auto channel_read1 = std::make_unique<fdf::ChannelRead>(
      local_ch_, 0,
      [&](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        ASSERT_OK(status);
        ASSERT_EQ(dispatcher1, fdf_dispatcher_get_current_dispatcher());
        // This reply will be reentrant and queued on the async loop.
        ASSERT_EQ(ZX_OK, fdf_channel_write(local_ch_, 0, nullptr, nullptr, 0, nullptr, 0));
      });
  ASSERT_OK(channel_read1->Begin(dispatcher1));

  libsync::Completion got_reply;
  auto channel_read2 = std::make_unique<fdf::ChannelRead>(
      remote_ch_, 0,
      [&](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
        ASSERT_OK(status);
        ASSERT_EQ(dispatcher2, fdf_dispatcher_get_current_dispatcher());
        got_reply.Signal();
      });
  ASSERT_OK(channel_read2->Begin(dispatcher2));

  // Write from driver 2 to driver1.
  ASSERT_OK(async::PostTask(fdf_dispatcher_get_async_dispatcher(dispatcher2), [&] {
    ASSERT_EQ(dispatcher2, fdf_dispatcher_get_current_dispatcher());
    // Non-reentrant write.
    ASSERT_EQ(ZX_OK, fdf_channel_write(remote_ch_, 0, nullptr, nullptr, 0, nullptr, 0));
  }));

  ASSERT_OK(got_reply.Wait(zx::time::infinite()));
  ASSERT_OK(fdf_internal_wait_until_dispatcher_idle(dispatcher2));
}

TEST_F(DispatcherTest, HasQueuedTasks) {
  fdf_dispatcher_t* dispatcher;
  ASSERT_NO_FATAL_FAILURE(CreateDispatcher(0, "scheduler_role", CreateFakeDriver(), &dispatcher));

  ASSERT_FALSE(dispatcher->HasQueuedTasks());

  // We shouldn't actually block on a dispatcher that doesn't have ALLOW_SYNC_CALLS set,
  // but this is just for synchronizing the test.
  libsync::Completion entered_callback;
  libsync::Completion complete_blocking_read;
  ASSERT_NO_FATAL_FAILURE(
      RegisterAsyncReadBlock(local_ch_, dispatcher, &entered_callback, &complete_blocking_read));

  // Call is reentrant, so the read will be queued on the async loop.
  ASSERT_EQ(ZX_OK, fdf_channel_write(remote_ch_, 0, nullptr, nullptr, 0, nullptr, 0));
  ASSERT_FALSE(dispatcher->IsIdle());

  // Wait for the read callback to be called, it will block until we signal it to complete.
  ASSERT_OK(entered_callback.Wait(zx::time::infinite()));

  libsync::Completion entered_task;
  ASSERT_OK(async::PostTask(dispatcher, [&] { entered_task.Signal(); }));
  ASSERT_TRUE(dispatcher->HasQueuedTasks());

  complete_blocking_read.Signal();

  ASSERT_OK(entered_task.Wait());
  ASSERT_FALSE(dispatcher->HasQueuedTasks());

  ASSERT_OK(fdf_internal_wait_until_dispatcher_idle(dispatcher));
  ASSERT_FALSE(dispatcher->HasQueuedTasks());
}

//
// Error handling
//

// Tests that you cannot create an unsynchronized blocking dispatcher.
TEST_F(DispatcherTest, CreateUnsynchronizedAllowSyncCallsFails) {
  driver_context::PushDriver(CreateFakeDriver());
  auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });

  DispatcherShutdownObserver observer(false /* require_callback */);
  driver_runtime::Dispatcher* dispatcher;
  uint32_t options = FDF_DISPATCHER_OPTION_UNSYNCHRONIZED | FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS;
  ASSERT_NE(ZX_OK, fdf_dispatcher::Create(options, "scheduler_role", 0, observer.fdf_observer(),
                                          &dispatcher));
}

// Tests that you cannot create a dispatcher on a thread not managed by the driver runtime.
TEST_F(DispatcherTest, CreateDispatcherOnNonRuntimeThreadFails) {
  DispatcherShutdownObserver observer(false /* require_callback */);
  driver_runtime::Dispatcher* dispatcher;
  ASSERT_NE(ZX_OK,
            fdf_dispatcher::Create(0, "scheduler_role", 0, observer.fdf_observer(), &dispatcher));
}
