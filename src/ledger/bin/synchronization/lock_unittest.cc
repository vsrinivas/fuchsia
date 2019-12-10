// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/synchronization/lock.h"

#include <lib/fit/function.h>

#include "gtest/gtest.h"
#include "src/ledger/lib/coroutine/coroutine_impl.h"

namespace lock {
namespace {
size_t Fact(size_t n) {
  if (n == 0) {
    return 1;
  }
  return Fact(n - 1) * n;
}

void UseStack() { EXPECT_EQ(Fact(5), 120u); }

TEST(Lock, OneLock) {
  coroutine::CoroutineServiceImpl coroutine_service;
  ledger::OperationSerializer serializer;

  fit::function<void(size_t)> callback;
  auto callable = [&callback](fit::function<void(size_t)> called_callback) {
    callback = std::move(called_callback);
  };

  size_t received_value = 0;
  size_t other_value = 0;
  coroutine_service.StartCoroutine(
      [&serializer, callable, &received_value](coroutine::CoroutineHandler* handler) {
        std::unique_ptr<Lock> lock;
        EXPECT_EQ(AcquireLock(handler, &serializer, &lock), coroutine::ContinuationStatus::OK);
        UseStack();
        size_t value;
        EXPECT_EQ(SyncCall(handler, callable, &value), coroutine::ContinuationStatus::OK);
        UseStack();
        received_value = value;
      });

  EXPECT_TRUE(callback);
  EXPECT_EQ(received_value, 0u);
  EXPECT_EQ(other_value, 0u);

  serializer.Serialize<>([&other_value] { other_value = 1u; },
                         [](fit::closure closure) { closure(); });

  EXPECT_EQ(other_value, 0u);
  callback(1);

  EXPECT_EQ(received_value, 1u);
  EXPECT_EQ(other_value, 1u);
}

TEST(Lock, ManyLocks) {
  constexpr size_t nb_routines = 10;
  coroutine::CoroutineServiceImpl coroutine_service;
  ledger::OperationSerializer serializer;

  std::queue<fit::function<void(size_t)>> callbacks;

  std::vector<size_t> received_values;
  for (size_t i = 0; i < nb_routines; i++) {
    auto callable = [&callbacks](fit::function<void(size_t)> called_callback) {
      callbacks.push(std::move(called_callback));
    };
    coroutine_service.StartCoroutine(
        [&serializer, callable, &received_values](coroutine::CoroutineHandler* handler) {
          std::unique_ptr<Lock> lock;
          EXPECT_EQ(AcquireLock(handler, &serializer, &lock), coroutine::ContinuationStatus::OK);
          UseStack();
          size_t value;
          EXPECT_EQ(SyncCall(handler, callable, &value), coroutine::ContinuationStatus::OK);
          UseStack();
          received_values.push_back(value);
        });
  }

  for (size_t i = 0; i < nb_routines; i++) {
    EXPECT_EQ(callbacks.size(), 1u);
    EXPECT_EQ(received_values.size(), i);
    callbacks.front()(i);
    callbacks.pop();
    EXPECT_EQ(*received_values.rbegin(), i);
  }
  EXPECT_EQ(received_values.size(), nb_routines);
}

TEST(Lock, Interrupted) {
  ledger::OperationSerializer serializer;
  coroutine::CoroutineHandler* handler_ptr = nullptr;

  fit::function<void()> callback;
  auto callable = [&callback](fit::function<void()> called_callback) {
    callback = std::move(called_callback);
  };

  serializer.Serialize<>([] {}, callable);

  bool executed = false;
  {
    coroutine::CoroutineServiceImpl coroutine_service;
    coroutine_service.StartCoroutine(
        [&serializer, &executed, &handler_ptr](coroutine::CoroutineHandler* handler) {
          handler_ptr = handler;
          std::unique_ptr<Lock> lock;
          // We are interrupted.
          EXPECT_EQ(AcquireLock(handler, &serializer, &lock),
                    coroutine::ContinuationStatus::INTERRUPTED);
          executed = true;
          return;
        });

    EXPECT_TRUE(callback);
    EXPECT_FALSE(executed);
  }

  EXPECT_TRUE(executed);

  callback();

  callback = nullptr;

  serializer.Serialize<>([] {}, callable);
  EXPECT_TRUE(callback);
}

}  // namespace
}  // namespace lock
