// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/lock/lock.h"

#include "gtest/gtest.h"
#include "lib/fxl/functional/closure.h"
#include "peridot/bin/ledger/coroutine/coroutine_impl.h"

namespace lock {
namespace {
size_t Fact(size_t n) {
  if (n == 0) {
    return 1;
  }
  return Fact(n - 1) * n;
}

void UseStack() { EXPECT_EQ(120u, Fact(5)); }

TEST(Lock, OneLock) {
  coroutine::CoroutineServiceImpl coroutine_service;
  callback::OperationSerializer serializer;

  std::function<void(size_t)> callback;
  auto callable = [&callback](std::function<void(size_t)> called_callback) {
    callback = std::move(called_callback);
  };

  size_t received_value = 0;
  size_t other_value = 0;
  coroutine_service.StartCoroutine([&serializer, callable, &received_value](
                                       coroutine::CoroutineHandler* handler) {
    std::unique_ptr<Lock> lock;
    EXPECT_EQ(coroutine::ContinuationStatus::OK,
              AcquireLock(handler, &serializer, &lock));
    UseStack();
    size_t value;
    EXPECT_EQ(coroutine::ContinuationStatus::OK,
              SyncCall(handler, callable, &value));
    UseStack();
    received_value = value;
  });

  EXPECT_TRUE(callback);
  EXPECT_EQ(0u, received_value);
  EXPECT_EQ(0u, other_value);

  serializer.Serialize<>([&other_value] { other_value = 1u; },
                         [](fxl::Closure closure) { closure(); });

  EXPECT_EQ(0u, other_value);
  callback(1);

  EXPECT_EQ(1u, received_value);
  EXPECT_EQ(1u, other_value);
}

TEST(Lock, ManyLocks) {
  constexpr size_t nb_routines = 10;
  coroutine::CoroutineServiceImpl coroutine_service;
  callback::OperationSerializer serializer;

  std::queue<std::function<void(size_t)>> callbacks;

  std::vector<size_t> received_values;
  for (size_t i = 0; i < nb_routines; i++) {
    auto callable = [&callbacks](std::function<void(size_t)> called_callback) {
      callbacks.push(std::move(called_callback));
    };
    coroutine_service.StartCoroutine([&serializer, callable, &received_values](
                                         coroutine::CoroutineHandler* handler) {
      std::unique_ptr<Lock> lock;
      EXPECT_EQ(coroutine::ContinuationStatus::OK,
                AcquireLock(handler, &serializer, &lock));
      UseStack();
      size_t value;
      EXPECT_EQ(coroutine::ContinuationStatus::OK,
                SyncCall(handler, callable, &value));
      UseStack();
      received_values.push_back(value);
    });
  }

  for (size_t i = 0; i < nb_routines; i++) {
    EXPECT_EQ(1u, callbacks.size());
    EXPECT_EQ(i, received_values.size());
    callbacks.front()(i);
    callbacks.pop();
    EXPECT_EQ(i, *received_values.rbegin());
  }
  EXPECT_EQ(nb_routines, received_values.size());
}

TEST(Lock, Interrupted) {
  callback::OperationSerializer serializer;
  coroutine::CoroutineHandler* handler_ptr = nullptr;

  std::function<void()> callback;
  auto callable = [&callback](std::function<void()> called_callback) {
    callback = std::move(called_callback);
  };

  serializer.Serialize<>([] {}, callable);

  bool executed = false;
  {
    coroutine::CoroutineServiceImpl coroutine_service;
    coroutine_service.StartCoroutine([&serializer, &executed, &handler_ptr](
                                         coroutine::CoroutineHandler* handler) {
      handler_ptr = handler;
      std::unique_ptr<Lock> lock;
      // We are interrupted.
      EXPECT_EQ(coroutine::ContinuationStatus::INTERRUPTED,
                AcquireLock(handler, &serializer, &lock));
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
