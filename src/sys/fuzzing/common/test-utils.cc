// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/test-utils.h"

#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

#include <gmock/gmock.h>

namespace fuzzing {

using ::testing::Eq;

std::atomic<TestBase*> TestBase::current_(nullptr);

TestBase::TestBase() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread), provider_(dispatcher()) {}

// gTest methods
void TestBase::SetUp() {
  ASSERT_EQ(current_.exchange(this), nullptr);
  ::testing::Test::SetUp();
  loop_.StartThread();
  context_ = provider_.TakeContext();
  sync_completion_reset(&sync_);
}

void TestBase::TearDown() {
  ASSERT_FALSE(sync_completion_signaled(&sync_));
  loop_.Quit();
  loop_.JoinThreads();
  ASSERT_EQ(loop_.ResetQuit(), ZX_OK);
  ::testing::Test::TearDown();
  ASSERT_EQ(current_.exchange(nullptr), this);
}

// Sync methods
void TestBase::Signal() {
  ASSERT_FALSE(sync_completion_signaled(&sync_));
  sync_completion_signal(&sync_);
}

void TestBase::Wait() {
  ASSERT_EQ(sync_completion_wait(&sync_, ZX_TIME_INFINITE), ZX_OK);
  sync_completion_reset(&sync_);
}

// Eventpair methods
void TestBase::SignalPeer(zx_signals_t signals) {
  ASSERT_EQ(ep_.signal_peer(0, signals & ZX_USER_SIGNAL_ALL), ZX_OK);
}

zx_signals_t TestBase::WaitOne() {
  zx_signals_t observed;
  if (ep_.wait_one(ZX_USER_SIGNAL_ALL, zx::time::infinite(), &observed) != ZX_OK ||
      ep_.signal(observed, 0) != ZX_OK) {
    return zx_signals_t(0);
  }
  return observed;
}

// Recording methods
TestBase* TestBase::Record(const std::string& func) {
  auto* current = current_.load();
  FX_CHECK(current);
  std::lock_guard<std::mutex> lock(current->mutex_);
  current->recorded_ = func;
  return current;
}

const char* TestBase::GetRecorded() {
  std::lock_guard<std::mutex> lock(mutex_);
  return recorded_.c_str();
}

void TestBase::SetU64(const std::string& key, uint64_t val) {
  std::lock_guard<std::mutex> lock(mutex_);
  u64s_[key] = val;
}

uint64_t TestBase::GetU64(const std::string& key) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto i = u64s_.find(key);
  FX_CHECK(i != u64s_.end());
  return i->second;
}

void TestBase::SetBytes(const std::string& key, const uint8_t* buf, size_t buf_len) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& bytes = bytes_[key];
  bytes.clear();
  bytes.insert(bytes.end(), buf, buf + buf_len);
}

std::vector<uint8_t> TestBase::GetBytes(const std::string& key) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto i = bytes_.find(key);
  if (i == bytes_.end()) {
    return std::vector<uint8_t>();
  }
  return i->second;
}

void TestBase::MatchBytes(const std::string& key, const std::vector<uint8_t>& bytes) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto i = bytes_.find(key);
  ASSERT_NE(i, bytes_.end());
  EXPECT_THAT(i->second, Eq(bytes));
}

}  // namespace fuzzing
