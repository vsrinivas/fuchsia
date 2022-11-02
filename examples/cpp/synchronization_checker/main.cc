// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ============================================================================
// This is an accompanying example code for writing thread safe code in C++.
// Head over there for the full walk-through:
// https://fuchsia.dev/fuchsia-src/development/languages/c-cpp/thread-safe-async
// ============================================================================

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/sequence_checker.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/channel.h>

#include <gtest/gtest.h>

namespace {

// [START synchronization_checker]
TEST(Async, SynchronizationCheckerExample) {
  // |ChannelReader| lets one asynchronously read from a Zircon channel.
  //
  // ## Thread safety
  //
  // Instances must be used from an async dispatcher with mutual exclusion
  // guarantee. See
  // https://fuchsia.dev/fuchsia-src/development/languages/c-cpp/thread-safe-async#mutual-exclusion-guarantee
  class ChannelReader {
   public:
    ChannelReader(async_dispatcher_t* dispatcher, zx::channel channel)
        : dispatcher_(dispatcher),
          checker_(dispatcher),
          channel_(std::move(channel)),
          wait_(channel_.get(), ZX_CHANNEL_READABLE) {}

    ~ChannelReader() {
      // |lock| explicitly checks that the dispatcher is not calling callbacks
      // that use this |ChannelReader| instance in the meantime.
      checker_.lock();
    }

    // Asynchronously wait for the channel to become readable, then read the
    // data into a member variable.
    void AsyncRead() {
      // This guard checks that the |AsyncRead| method is called from a task
      // running on a dispatcher with mutual exclusion guarantee.
      std::lock_guard guard(checker_);

      data_.clear();

      zx_status_t status = wait_.Begin(
          // The dispatcher that will perform the waiting.
          dispatcher_,

          // The async dispatcher will call this callback when the channel is
          // ready to be read from. Because this callback captures `this`, we
          // must ensure the callback does not race with destroying the
          // |ChannelReader| instance. This is accomplished by calling
          // `checker_.lock()` in the |ChannelReader| destructor.
          [this](async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                 const zx_packet_signal_t* signal) {
            if (status != ZX_OK) {
              return;
            }
            std::lock_guard guard(checker_);

            uint32_t actual;
            data_.resize(ZX_CHANNEL_MAX_MSG_BYTES);
            status = channel_.read(0, data_.data(), nullptr,
                                   static_cast<uint32_t>(data_.capacity()), 0, &actual, nullptr);
            if (status != ZX_OK) {
              data_.clear();
              return;
            }
            data_.resize(actual);
          });
      ZX_ASSERT(status == ZX_OK);
    }

    std::vector<uint8_t> data() const {
      // Here we also verify synchronization, because we want to avoid race
      // conditions such as the user calling |AsyncRead| which clears the
      // data and calling |data| to get the data at the same time.
      std::lock_guard guard(checker_);

      return data_;
    }

   private:
    async_dispatcher_t* dispatcher_;
    async::synchronization_checker checker_;
    zx::channel channel_;
    std::vector<uint8_t> data_ __TA_GUARDED(checker_);
    async::WaitOnce wait_;
  };

  zx::channel c1, c2;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &c1, &c2));

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  ChannelReader reader{loop.dispatcher(), std::move(c1)};

  ASSERT_EQ(reader.data(), std::vector<uint8_t>{});

  const std::vector<uint8_t> kData{1, 2, 3};
  ASSERT_EQ(ZX_OK, c2.write(0, kData.data(), static_cast<uint32_t>(kData.size()), nullptr, 0));

  // Using |reader| must be synchronized with dispatching asynchronous operations.
  // Here, they are synchronized because we perform these one after the other
  // from a single thread.
  reader.AsyncRead();
  loop.RunUntilIdle();

  ASSERT_EQ(reader.data(), kData);

  // The following is disallowed, and would lead to a panic.
  // If the dispatcher is running from a different thread, then we cannot
  // ensure that |reader| is not used in the meantime.
  //
  // std::thread([&] { loop.RunUntilIdle(); }).join();
}
// [END synchronization_checker]

}  // namespace
