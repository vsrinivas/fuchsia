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
    const char* error;
    async_sequence_id_t sequence_id;
  };

  zx_status_t GetSequenceId(async_sequence_id_t* out_sequence_id, const char** out_error) override {
    if (answer_.status != ZX_OK) {
      *out_error = answer_.error;
      return answer_.status;
    }
    *out_sequence_id = answer_.sequence_id;
    return ZX_OK;
  }

  zx_status_t CheckSequenceId(async_sequence_id_t sequence_id, const char** out_error) override {
    async_sequence_id_t current_sequence_id;
    zx_status_t status = GetSequenceId(&current_sequence_id, out_error);
    if (status != ZX_OK) {
      return status;
    }
    if (current_sequence_id.value != sequence_id.value) {
      *out_error = "test sequence id mismatch";
      return ZX_ERR_OUT_OF_RANGE;
    }
    return ZX_OK;
  }

  void SetSequenceIdAnswer(async_sequence_id_t id) {
    answer_ = {.status = ZX_OK, .sequence_id = id};
  }

  void SetSequenceIdAnswer(zx_status_t status, const char* error) {
    answer_ = {.status = status, .error = error, .sequence_id = {}};
  }

 private:
  SequenceIdAnswer answer_ = {};
};

TEST(SequenceChecker, SameSequenceId) {
  FakeSequenceIdAsync dispatcher;
  dispatcher.SetSequenceIdAnswer({.value = 1});
  async::sequence_checker checker{&dispatcher};
  EXPECT_TRUE(cpp17::holds_alternative<cpp17::monostate>(checker.is_sequence_valid()));
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
  EXPECT_TRUE(cpp17::holds_alternative<std::string>(checker.is_sequence_valid()));
  EXPECT_SUBSTR(cpp17::get<std::string>(checker.is_sequence_valid()), "test sequence id mismatch");
}

TEST(SequenceChecker, NoSequenceId) {
  FakeSequenceIdAsync dispatcher;
  dispatcher.SetSequenceIdAnswer(ZX_ERR_INVALID_ARGS, "");
  ASSERT_DEATH([&] { async::sequence_checker checker{&dispatcher}; });
  dispatcher.SetSequenceIdAnswer(ZX_ERR_WRONG_TYPE, "");
  ASSERT_DEATH([&] { async::sequence_checker checker{&dispatcher}; });
  dispatcher.SetSequenceIdAnswer(ZX_ERR_NOT_SUPPORTED, "");
  ASSERT_DEATH([&] { async::sequence_checker checker{&dispatcher}; });
}

TEST(SequenceChecker, ConcatError) {
  FakeSequenceIdAsync dispatcher;
  dispatcher.SetSequenceIdAnswer({.value = 1});
  async::sequence_checker checker{&dispatcher, "|Foo| is thread unsafe."};
  dispatcher.SetSequenceIdAnswer(ZX_ERR_INVALID_ARGS, "Switch to another dispatcher.");
  EXPECT_TRUE(cpp17::holds_alternative<std::string>(checker.is_sequence_valid()));
  EXPECT_SUBSTR(cpp17::get<std::string>(checker.is_sequence_valid()),
                "|Foo| is thread unsafe. Switch to another dispatcher.");
}

TEST(SynchronizationChecker, SameSequenceId) {
  FakeSequenceIdAsync dispatcher;
  dispatcher.SetSequenceIdAnswer({.value = 1});
  async::synchronization_checker checker{&dispatcher};
  EXPECT_TRUE(cpp17::holds_alternative<cpp17::monostate>(checker.is_synchronized()));
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
  EXPECT_TRUE(cpp17::holds_alternative<std::string>(checker.is_synchronized()));
  EXPECT_SUBSTR(cpp17::get<std::string>(checker.is_synchronized()), "test sequence id mismatch");
}

TEST(SynchronizationChecker, SameThreadId) {
  FakeSequenceIdAsync dispatcher;
  dispatcher.SetSequenceIdAnswer(ZX_ERR_NOT_SUPPORTED, "");
  async::synchronization_checker checker{&dispatcher};
  EXPECT_TRUE(cpp17::holds_alternative<cpp17::monostate>(checker.is_synchronized()));
}

TEST(SynchronizationChecker, DifferentThreadId) {
  FakeSequenceIdAsync dispatcher;
  dispatcher.SetSequenceIdAnswer(ZX_ERR_NOT_SUPPORTED, "");
  async::synchronization_checker checker{&dispatcher};
  EXPECT_TRUE(cpp17::holds_alternative<cpp17::monostate>(checker.is_synchronized()));
  std::thread t([&] {
    EXPECT_TRUE(cpp17::holds_alternative<std::string>(checker.is_synchronized()));
    EXPECT_SUBSTR(cpp17::get<std::string>(checker.is_synchronized()),
                  "Access from multiple threads detected");
  });
  t.join();
}

TEST(SynchronizationChecker, SequenceIdThenThreadId) {
  FakeSequenceIdAsync dispatcher;
  dispatcher.SetSequenceIdAnswer({.value = 1});
  async::synchronization_checker checker{&dispatcher};
  EXPECT_TRUE(cpp17::holds_alternative<cpp17::monostate>(checker.is_synchronized()));
  dispatcher.SetSequenceIdAnswer(ZX_ERR_INVALID_ARGS, "");
  EXPECT_TRUE(cpp17::holds_alternative<std::string>(checker.is_synchronized()));
  ASSERT_DEATH([&] {
    dispatcher.SetSequenceIdAnswer(ZX_ERR_INVALID_ARGS, "");
    checker.lock();
    checker.unlock();
  });
  dispatcher.SetSequenceIdAnswer(ZX_ERR_WRONG_TYPE, "");
  EXPECT_TRUE(cpp17::holds_alternative<std::string>(checker.is_synchronized()));
  ASSERT_DEATH([&] {
    dispatcher.SetSequenceIdAnswer(ZX_ERR_WRONG_TYPE, "");
    checker.lock();
    checker.unlock();
  });
  dispatcher.SetSequenceIdAnswer(ZX_ERR_NOT_SUPPORTED, "");
  EXPECT_TRUE(cpp17::holds_alternative<std::string>(checker.is_synchronized()));
  ASSERT_DEATH([&] {
    dispatcher.SetSequenceIdAnswer(ZX_ERR_NOT_SUPPORTED, "");
    checker.lock();
    checker.unlock();
  });
}

TEST(SynchronizationChecker, SequenceConcatError) {
  FakeSequenceIdAsync dispatcher;
  dispatcher.SetSequenceIdAnswer({.value = 1});
  async::synchronization_checker checker{&dispatcher, "|Foo| is thread unsafe."};
  dispatcher.SetSequenceIdAnswer(ZX_ERR_INVALID_ARGS, "Switch to another dispatcher.");
  EXPECT_TRUE(cpp17::holds_alternative<std::string>(checker.is_synchronized()));
  EXPECT_SUBSTR(cpp17::get<std::string>(checker.is_synchronized()),
                "|Foo| is thread unsafe. Switch to another dispatcher.");
}

TEST(SynchronizationChecker, ThreadConcatError) {
  FakeSequenceIdAsync dispatcher;
  dispatcher.SetSequenceIdAnswer(ZX_ERR_NOT_SUPPORTED, "");
  async::synchronization_checker checker{&dispatcher, "|Foo| is thread unsafe."};
  std::thread([&] {
    EXPECT_TRUE(cpp17::holds_alternative<std::string>(checker.is_synchronized()));
    EXPECT_SUBSTR(cpp17::get<std::string>(checker.is_synchronized()),
                  "|Foo| is thread unsafe. Access from multiple threads detected. "
                  "This is not allowed. Ensure the object is used from the same thread.");
  }).join();
}

}  // namespace
