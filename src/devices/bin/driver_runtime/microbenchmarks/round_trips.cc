// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <lib/fdf/cpp/channel.h>
#include <lib/fdf/cpp/channel_read.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fdf/internal.h>
#include <lib/fit/function.h>
#include <lib/sync/completion.h>
#include <lib/sync/cpp/completion.h>
#include <lib/syslog/cpp/macros.h>

#include <thread>
#include <vector>

#include "src/devices/bin/driver_runtime/microbenchmarks/assert.h"
#include "src/devices/bin/driver_runtime/microbenchmarks/driver_stack_manager.h"
#include "src/devices/bin/driver_runtime/microbenchmarks/test_runner.h"

namespace {

// Registers an asynchronous channel read handler with |dispatcher|.
// The read handler will read |count| messages, re-registering the read handler if necessary.
// If |reply| is true, the read messages will be written back to the channel.
zx::status<std::unique_ptr<fdf::ChannelRead>> RegisterChannelReadMultiple(
    const fdf::Channel& channel, const fdf::Dispatcher& dispatcher, uint32_t want_num_read,
    bool reply, uint32_t want_msg_size, sync_completion_t* completion) {
  auto channel_read = std::make_unique<fdf::ChannelRead>(
      channel.get(), 0 /* options */,
      [=, num_read = 0u](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read,
                         fdf_status_t status) mutable {
        ASSERT_OK(status);

        fdf::UnownedChannel channel(channel_read->channel());
        while (num_read < want_num_read) {
          auto read = channel->Read(0);
          if (read.status_value() == ZX_ERR_SHOULD_WAIT) {
            // Ran out of messages to read, need to register for another callback.
            ASSERT_OK(channel_read->Begin(dispatcher));
            return;
          }
          ASSERT_OK(read.status_value());
          FX_CHECK(read->num_bytes == want_msg_size);
          if (reply) {
            ASSERT_OK(
                channel
                    ->Write(0, read->arena, read->data, read->num_bytes, cpp20::span<zx_handle_t>())
                    .status_value());
          }
          num_read++;
        }
        FX_CHECK(num_read == want_num_read);
        sync_completion_signal(completion);
      });

  fdf_status_t status = channel_read->Begin(dispatcher.get());
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(channel_read));
}

// Test IPC round trips using fdf channels where the client and server
// both use the same kind of fdf dispatchers to wait.
class ChannelDispatcherTest {
 public:
  explicit ChannelDispatcherTest(uint32_t dispatcher_options, uint32_t msg_count, uint32_t msg_size)
      : msg_count_(msg_count), msg_size_(msg_size) {
    auto channel_pair = fdf::ChannelPair::Create(0);
    ASSERT_OK(channel_pair.status_value());

    client_ = std::move(channel_pair->end0);
    server_ = std::move(channel_pair->end1);

    {
      DriverStackManager dsm(&client_fake_driver_);
      auto dispatcher = fdf::Dispatcher::Create(
          dispatcher_options, fit::bind_member(this, &ChannelDispatcherTest::ShutdownHandler));
      ASSERT_OK(dispatcher.status_value());
      client_dispatcher_ = *std::move(dispatcher);
    }

    {
      DriverStackManager dsm(&server_fake_driver_);
      auto dispatcher = fdf::Dispatcher::Create(
          dispatcher_options, fit::bind_member(this, &ChannelDispatcherTest::ShutdownHandler));
      ASSERT_OK(dispatcher.status_value());
      server_dispatcher_ = *std::move(dispatcher);
    }

    std::string_view tag;
    auto arena = fdf::Arena::Create(0, tag);
    ASSERT_OK(arena.status_value());
    arena_ = *std::move(arena);

    // Create the messages to transfer.
    for (uint32_t i = 0; i < msg_count_; i++) {
      msgs_.push_back(arena_.Allocate(msg_size_));
    }
  }

  void Run() {
    sync_completion_t client_completion;
    sync_completion_t server_completion;
    auto client_read = RegisterChannelReadMultiple(
        client_, client_dispatcher_, msg_count_, false /* reply */, msg_size_, &client_completion);
    auto server_read = RegisterChannelReadMultiple(server_, server_dispatcher_, msg_count_,
                                                   true /* reply */, msg_size_, &server_completion);

    ASSERT_OK(client_read.status_value());
    ASSERT_OK(server_read.status_value());

    // Send the messages from client to server.
    async_dispatcher_t* async_dispatcher = client_dispatcher_.async_dispatcher();
    FX_CHECK(async_dispatcher != nullptr);

    ASSERT_OK(async::PostTask(async_dispatcher, [&, this] {
      DriverStackManager dsm(&client_fake_driver_);
      for (const auto& msg : msgs_) {
        ASSERT_OK(
            client_.Write(0, arena_, msg, msg_size_, cpp20::span<zx_handle_t>()).status_value());
      }
    }));
    ASSERT_OK(sync_completion_wait(&client_completion, ZX_TIME_INFINITE));
    ASSERT_OK(sync_completion_wait(&server_completion, ZX_TIME_INFINITE));
  }

  void TearDown() {
    client_dispatcher_.ShutdownAsync();
    server_dispatcher_.ShutdownAsync();
    ASSERT_OK(client_dispatcher_shutdown_.Wait());
    ASSERT_OK(server_dispatcher_shutdown_.Wait());
  }

  void ShutdownHandler(fdf_dispatcher_t* dispatcher) {
    FX_CHECK((dispatcher == client_dispatcher_.get()) || (dispatcher == server_dispatcher_.get()));
    if (dispatcher == client_dispatcher_.get()) {
      client_dispatcher_shutdown_.Signal();
    } else {
      server_dispatcher_shutdown_.Signal();
    }
  }

 private:
  uint32_t msg_count_;
  uint32_t msg_size_;

  // Arena-allocated messages to transfer.
  std::vector<void*> msgs_;

  fdf::Channel client_;
  fdf::Dispatcher client_dispatcher_;
  libsync::Completion client_dispatcher_shutdown_;

  fdf::Channel server_;
  fdf::Dispatcher server_dispatcher_;
  libsync::Completion server_dispatcher_shutdown_;

  fdf::Arena arena_;

  uint32_t client_fake_driver_;
  uint32_t server_fake_driver_;
};

void RegisterTests() {
  driver_runtime_benchmark::RegisterTest<ChannelDispatcherTest>(
      "RoundTrip_ChannelPort_Synchronized",
      /* dispatcher_options= */ 0,
      /* msg_count= */ 1, /* msg_size= */ 4);
  driver_runtime_benchmark::RegisterTest<ChannelDispatcherTest>(
      "RoundTrip_ChannelPort_AllowSyncCalls",
      /* dispatcher_options = */ FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS,
      /* msg_count= */ 1, /* msg_size= */ 4);

  driver_runtime_benchmark::RegisterTest<ChannelDispatcherTest>(
      "IpcThroughput_BasicChannel_1_64kbytes", /* dispatcher_options= */ 0,
      /* msg_count= */ 1, /* msg_size= */ 64 * 1024);
  driver_runtime_benchmark::RegisterTest<ChannelDispatcherTest>(
      "IpcThroughput_BasicChannel_1024_4bytes",
      /* dispatcher_options= */ 0,
      /* msg_count= */ 1024, /* msg_size= */ 4);
  driver_runtime_benchmark::RegisterTest<ChannelDispatcherTest>(
      "IpcThroughput_BasicChannel_1024_64kbytes", /* dispatcher_options= */ 0,
      /* msg_count= */ 1024, /* msg_size= */ 64 * 1024);
}
PERFTEST_CTOR(RegisterTests)

}  // namespace
