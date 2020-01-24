// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sync/completion.h>
#include <lib/zx/time.h>
#include <stdint.h>

#include <array>
#include <thread>

#include <cobalt-client/cpp/collector.h>
#include <cobalt-client/cpp/in_memory_logger.h>
#include <cobalt-client/cpp/types_internal.h>
#include <zxtest/zxtest.h>

namespace cobalt_client {
namespace internal {
namespace {

// Fake Flushable used to keep track of calls to individual flushables.
class FakeFlushable final : public internal::FlushInterface {
 public:
  FakeFlushable() = default;
  FakeFlushable(const FakeFlushable&) = delete;
  FakeFlushable(FakeFlushable&&) = delete;
  FakeFlushable& operator=(const FakeFlushable&) = delete;
  FakeFlushable& operator=(FakeFlushable&&) = delete;
  ~FakeFlushable() final {}

  bool Flush(Logger* logger) final {
    flush_count_++;
    return !fail_flush();
  }

  void UndoFlush() final { undo_flush_count_++; }

  // Observation traits.
  uint32_t flush_count() const { return flush_count_.load(); }
  uint32_t undo_flush_count() const { return undo_flush_count_.load(); }
  bool fail_flush() const { return fail_flush_; }

  // Behavior control.
  bool* mutable_fail_flush() { return &fail_flush_; }

 private:
  bool fail_flush_ = false;

  std::atomic<uint32_t> flush_count_ = 0;
  std::atomic<uint32_t> undo_flush_count_ = 0;
};

// Fake implementation that stalls calls to Flush and undo flush until signaled.
class StallingFlushable final : public internal::FlushInterface {
 public:
  StallingFlushable() = default;
  StallingFlushable(const StallingFlushable&) = delete;
  StallingFlushable(StallingFlushable&&) = delete;
  StallingFlushable& operator=(const StallingFlushable&) = delete;
  StallingFlushable& operator=(StallingFlushable&&) = delete;
  ~StallingFlushable() final {}

  bool Flush(Logger* logger) final {
    flush_count_++;
    sync_completion_signal(&flush_started_signal_);
    sync_completion_wait(&flush_signal_, zx::duration::infinite().get());
    return !fail_flush();
  }

  void UndoFlush() final {
    undo_flush_count_++;
    sync_completion_wait(&undo_flush_signal_, zx::duration::infinite().get());
  }

  uint32_t flush_count() const { return flush_count_.load(); }
  uint32_t undo_flush_count() const { return undo_flush_count_.load(); }

  bool fail_flush() const { return fail_flush_; }

  // Behavior control.
  bool* mutable_fail_flush() { return &fail_flush_; }

  void WaitUntilFlushStarts() {
    sync_completion_wait(&flush_started_signal_, zx::duration::infinite().get());
  }
  void ResumeFlush() { sync_completion_signal(&flush_signal_); }
  void ResumeUndoFlush() { sync_completion_signal(&undo_flush_signal_); }

 private:
  bool fail_flush_ = false;

  std::atomic<uint32_t> flush_count_ = 0;
  std::atomic<uint32_t> undo_flush_count_ = 0;

  sync_completion_t flush_signal_;
  sync_completion_t flush_started_signal_;
  sync_completion_t undo_flush_signal_;
};

// Default Params for Collector Options.
constexpr uint32_t kProjectId = 1234;

TEST(CollectorTest, CreateIsSuccessfull) {
  ASSERT_NO_DEATH([] { Collector collector(kProjectId); });
}

TEST(CollectorTest, CreateFromInvalidIdTriggersAssert) {
  ASSERT_DEATH([] { Collector collector(0); });
}

TEST(CollectorTest, FlushFlushEachSubscriptor) {
  constexpr uint32_t kFlushableCount = 20;
  std::array<FakeFlushable, kFlushableCount> flushables = {};
  Collector collector(std::make_unique<InMemoryLogger>());

  for (uint32_t i = 0; i < kFlushableCount; ++i) {
    collector.Subscribe(&flushables[i]);
  }

  ASSERT_TRUE(collector.Flush());

  for (auto& flushable : flushables) {
    EXPECT_EQ(1, flushable.flush_count());
    EXPECT_EQ(0, flushable.undo_flush_count());
  }
}

TEST(CollectorTest, FlushUndoFlushEachSubscriptorOnFailureAndReturnsFalse) {
  constexpr uint32_t kFlushableCount = 20;
  std::array<FakeFlushable, kFlushableCount> flushables = {};
  Collector collector(std::make_unique<InMemoryLogger>());

  for (uint32_t i = 0; i < kFlushableCount; ++i) {
    *flushables[i].mutable_fail_flush() = true;
    collector.Subscribe(&flushables[i]);
  }

  ASSERT_FALSE(collector.Flush());

  for (auto& flushable : flushables) {
    EXPECT_EQ(1, flushable.flush_count());
    EXPECT_EQ(1, flushable.undo_flush_count());
  }
}

TEST(CollectorTest, FlushUndosApplyIndividuallyOnIndividualLogFailuresAndReturnsFalse) {
  constexpr uint32_t kFlushableCount = 20;
  std::array<FakeFlushable, kFlushableCount> flushables = {};
  Collector collector(std::make_unique<InMemoryLogger>());

  for (uint32_t i = 0; i < kFlushableCount; ++i) {
    if (i % 2 == 0) {
      *flushables[i].mutable_fail_flush() = true;
    }
    collector.Subscribe(&flushables[i]);
  }

  ASSERT_FALSE(collector.Flush());

  for (auto& flushable : flushables) {
    uint32_t expected_undo_count = flushable.fail_flush() ? 1 : 0;
    EXPECT_EQ(1, flushable.flush_count());
    EXPECT_EQ(expected_undo_count, flushable.undo_flush_count());
  }
}

TEST(CollectorTest, FlushCalledIgnoredWhileFlushIsInProgress) {
  constexpr uint32_t kFlushableCount = 20;
  std::array<FakeFlushable, kFlushableCount> flushables = {};
  StallingFlushable stalling_flushable;
  Collector collector(std::make_unique<InMemoryLogger>());
  std::atomic<bool> flush_thread_result(false);

  for (uint32_t i = 0; i < kFlushableCount; ++i) {
    collector.Subscribe(&flushables[i]);
  }
  collector.Subscribe(&stalling_flushable);

  std::thread flush_thread(
      [&collector, &flush_thread_result]() { flush_thread_result.store(collector.Flush()); });

  stalling_flushable.WaitUntilFlushStarts();
  ASSERT_FALSE(collector.Flush());

  stalling_flushable.ResumeFlush();
  stalling_flushable.ResumeUndoFlush();

  flush_thread.join();
  ASSERT_TRUE(flush_thread_result.load());

  for (auto& flushable : flushables) {
    EXPECT_EQ(1, flushable.flush_count());
    EXPECT_EQ(0, flushable.undo_flush_count());
  }

  EXPECT_EQ(1, stalling_flushable.flush_count());
  EXPECT_EQ(0, stalling_flushable.undo_flush_count());

  ASSERT_TRUE(collector.Flush());
}

}  // namespace
}  // namespace internal
}  // namespace cobalt_client
