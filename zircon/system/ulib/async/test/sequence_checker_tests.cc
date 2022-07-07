// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testing/dispatcher_stub.h>
#include <lib/async/cpp/sequence_checker.h>

#include <zxtest/zxtest.h>

namespace {

class FakeSequenceIdAsync : public async::DispatcherStub {
 public:
  struct SequenceIdAnswer {
    zx_status_t status;
    async_sequence_id_t sequence_id;
  };

  zx_status_t GetSequenceId(async_sequence_id_t* out_sequence_id) override {
    if (answer_.status != ZX_OK) {
      return answer_.status;
    }
    *out_sequence_id = answer_.sequence_id;
    return ZX_OK;
  }

  void SetSequenceIdAnswer(async_sequence_id_t id) {
    answer_ = {.status = ZX_OK, .sequence_id = id};
  }

  void SetSequenceIdAnswer(zx_status_t status) { answer_ = {.status = status, .sequence_id = {}}; }

 private:
  SequenceIdAnswer answer_ = {};
};

TEST(SequenceChecker, SameSequenceId) {
  FakeSequenceIdAsync dispatcher;
  dispatcher.SetSequenceIdAnswer({.value = 1});
  async::sequence_checker checker{&dispatcher};
  EXPECT_TRUE(checker.is_sequence_valid());
}

TEST(SequenceChecker, LockUnlock) {
  FakeSequenceIdAsync dispatcher;
  dispatcher.SetSequenceIdAnswer({.value = 1});
  async::sequence_checker checker{&dispatcher};
  checker.lock();
  checker.unlock();
  std::lock_guard<async::sequence_checker> locker(checker);
}

TEST(SequenceChecker, DifferentSequenceId) {
  FakeSequenceIdAsync dispatcher;
  dispatcher.SetSequenceIdAnswer({.value = 1});
  async::sequence_checker checker{&dispatcher};
  dispatcher.SetSequenceIdAnswer({.value = 2});
  EXPECT_FALSE(checker.is_sequence_valid());
}

TEST(SequenceChecker, NoSequenceId) {
  FakeSequenceIdAsync dispatcher;
  dispatcher.SetSequenceIdAnswer(ZX_ERR_INVALID_ARGS);
  ASSERT_DEATH([&] { async::sequence_checker checker{&dispatcher}; });
  dispatcher.SetSequenceIdAnswer(ZX_ERR_WRONG_TYPE);
  ASSERT_DEATH([&] { async::sequence_checker checker{&dispatcher}; });
  dispatcher.SetSequenceIdAnswer(ZX_ERR_NOT_SUPPORTED);
  ASSERT_DEATH([&] { async::sequence_checker checker{&dispatcher}; });
}

TEST(SynchronizationChecker, SameSequenceId) {
  FakeSequenceIdAsync dispatcher;
  dispatcher.SetSequenceIdAnswer({.value = 1});
  async::synchronization_checker checker{&dispatcher};
  EXPECT_TRUE(checker.is_synchronized());
}

TEST(SynchronizationChecker, LockUnlock) {
  FakeSequenceIdAsync dispatcher;
  dispatcher.SetSequenceIdAnswer({.value = 1});
  async::synchronization_checker checker{&dispatcher};
  checker.lock();
  checker.unlock();
  std::lock_guard<async::synchronization_checker> locker(checker);
}

TEST(SynchronizationChecker, DifferentSequenceId) {
  FakeSequenceIdAsync dispatcher;
  dispatcher.SetSequenceIdAnswer({.value = 1});
  async::synchronization_checker checker{&dispatcher};
  dispatcher.SetSequenceIdAnswer({.value = 2});
  EXPECT_FALSE(checker.is_synchronized());
}

TEST(SynchronizationChecker, SameThreadId) {
  FakeSequenceIdAsync dispatcher;
  dispatcher.SetSequenceIdAnswer(ZX_ERR_NOT_SUPPORTED);
  async::synchronization_checker checker{&dispatcher};
  EXPECT_TRUE(checker.is_synchronized());
}

TEST(SynchronizationChecker, DifferentThreadId) {
  FakeSequenceIdAsync dispatcher;
  dispatcher.SetSequenceIdAnswer(ZX_ERR_NOT_SUPPORTED);
  async::synchronization_checker checker{&dispatcher};
  EXPECT_TRUE(checker.is_synchronized());
  std::thread t([&] { EXPECT_FALSE(checker.is_synchronized()); });
  t.join();
}

TEST(SynchronizationChecker, SequenceIdThenThreadId) {
  FakeSequenceIdAsync dispatcher;
  dispatcher.SetSequenceIdAnswer({.value = 1});
  async::synchronization_checker checker{&dispatcher};
  EXPECT_TRUE(checker.is_synchronized());
  ASSERT_DEATH([&] {
    dispatcher.SetSequenceIdAnswer(ZX_ERR_INVALID_ARGS);
    (void)checker.is_synchronized();
  });
  ASSERT_DEATH([&] {
    dispatcher.SetSequenceIdAnswer(ZX_ERR_WRONG_TYPE);
    (void)checker.is_synchronized();
  });
  ASSERT_DEATH([&] {
    dispatcher.SetSequenceIdAnswer(ZX_ERR_NOT_SUPPORTED);
    (void)checker.is_synchronized();
  });
}

}  // namespace
