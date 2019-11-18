// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/coroutine/coroutine_manager.h"

#include <memory>

#include "gtest/gtest.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/ledger/lib/coroutine/coroutine_impl.h"
#include "src/lib/callback/set_when_called.h"

namespace coroutine {
namespace {

// Parametrize tests with the maximum number of coroutines (0 = unlimited).
using CoroutineManagerTest = ::testing::TestWithParam<size_t>;

TEST_P(CoroutineManagerTest, CallbackIsCalled) {
  CoroutineServiceImpl coroutine_service;
  CoroutineManager manager(&coroutine_service, GetParam());

  bool called = false;
  CoroutineHandler* handler = nullptr;
  manager.StartCoroutine(
      callback::SetWhenCalled(&called),
      [&handler](CoroutineHandler* current_handler, fit::function<void()> callback) {
        handler = current_handler;
        EXPECT_EQ(handler->Yield(), ContinuationStatus::OK);
        callback();
      });

  ASSERT_TRUE(handler);
  EXPECT_FALSE(called);
  handler->Resume(ContinuationStatus::OK);
  EXPECT_TRUE(called);
}

TEST_P(CoroutineManagerTest, MultipleCoroutines) {
  CoroutineServiceImpl coroutine_service;
  CoroutineManager manager(&coroutine_service, GetParam());

  bool called = false;
  CoroutineHandler* handler = nullptr;
  manager.StartCoroutine(
      callback::SetWhenCalled(&called),
      [&handler](CoroutineHandler* current_handler, fit::function<void()> callback) {
        handler = current_handler;
        EXPECT_EQ(handler->Yield(), ContinuationStatus::OK);
        callback();
      });

  bool called_2 = false;
  CoroutineHandler* handler_2 = nullptr;
  manager.StartCoroutine(
      callback::SetWhenCalled(&called_2),
      [&handler_2](CoroutineHandler* current_handler_2, fit::function<void()> callback) {
        handler_2 = current_handler_2;
        EXPECT_EQ(handler_2->Yield(), ContinuationStatus::OK);
        callback();
      });

  // Coroutine 1 has yielded before completion.
  EXPECT_FALSE(called);
  // Coroutine 2 has yielded before completion, or the task is pending (if GetParam() == 1).
  EXPECT_FALSE(called_2);
  // Resume first coroutine.
  ASSERT_TRUE(handler);
  handler->Resume(ContinuationStatus::OK);
  EXPECT_TRUE(called);
  EXPECT_FALSE(called_2);
  // Resume second coroutine (which may be the same as the first if GetParam() == 1).
  ASSERT_TRUE(handler_2);
  handler_2->Resume(ContinuationStatus::OK);
  EXPECT_TRUE(called_2);
}

TEST_P(CoroutineManagerTest, InterruptCoroutineOnDestruction) {
  CoroutineServiceImpl coroutine_service;
  std::unique_ptr<CoroutineManager> manager =
      std::make_unique<CoroutineManager>(&coroutine_service, GetParam());

  bool called = false;
  bool reached_callback = false;
  bool executed_callback = false;
  CoroutineHandler* handler = nullptr;
  manager->StartCoroutine(callback::SetWhenCalled(&called),
                          [&](CoroutineHandler* current_handler, fit::function<void()> callback) {
                            handler = current_handler;
                            EXPECT_EQ(handler->Yield(), ContinuationStatus::INTERRUPTED);
                            reached_callback = true;
                            callback();
                            executed_callback = true;
                          });

  ASSERT_TRUE(handler);
  EXPECT_FALSE(called);
  EXPECT_FALSE(reached_callback);
  manager.reset();
  EXPECT_FALSE(called);
  // The manager is shutting down, so |SetWhenCalled| is not called when
  // |callback| is called.
  EXPECT_TRUE(executed_callback);
}

TEST_P(CoroutineManagerTest, CoroutineCallbackStartsCoroutine) {
  CoroutineServiceImpl coroutine_service;
  CoroutineManager manager(&coroutine_service, GetParam());

  bool called = false;
  bool called_2 = false;
  CoroutineHandler* handler = nullptr;
  CoroutineHandler* handler_2 = nullptr;
  // The callback of the first coroutine starts a second coroutine.
  auto callback = [&called, &manager, &called_2, &handler_2] {
    called = true;
    manager.StartCoroutine(
        callback::SetWhenCalled(&called_2),
        [&handler_2](CoroutineHandler* current_handler_2, fit::function<void()> callback) {
          handler_2 = current_handler_2;
          EXPECT_EQ(handler_2->Yield(), ContinuationStatus::OK);
          callback();
        });
  };
  manager.StartCoroutine(std::move(callback), [&handler](CoroutineHandler* current_handler,
                                                         fit::function<void()> callback) {
    handler = current_handler;
    EXPECT_EQ(handler->Yield(), ContinuationStatus::OK);
    callback();
  });

  ASSERT_TRUE(handler);
  EXPECT_FALSE(called);
  EXPECT_FALSE(handler_2);
  EXPECT_FALSE(called_2);
  handler->Resume(ContinuationStatus::OK);
  EXPECT_TRUE(called);
  ASSERT_TRUE(handler_2);
  EXPECT_FALSE(called_2);
  handler_2->Resume(ContinuationStatus::OK);
  EXPECT_TRUE(called_2);
}

TEST_P(CoroutineManagerTest, Shutdown) {
  CoroutineServiceImpl coroutine_service;
  CoroutineManager manager(&coroutine_service, GetParam());

  bool called = false;
  bool reached_callback = false;
  bool executed_callback = false;
  CoroutineHandler* handler = nullptr;
  manager.StartCoroutine(callback::SetWhenCalled(&called),
                         [&](CoroutineHandler* current_handler, fit::function<void()> callback) {
                           handler = current_handler;
                           EXPECT_EQ(handler->Yield(), ContinuationStatus::INTERRUPTED);
                           reached_callback = true;
                           callback();
                           executed_callback = true;
                         });

  ASSERT_TRUE(handler);
  EXPECT_FALSE(called);
  EXPECT_FALSE(reached_callback);
  manager.Shutdown();
  EXPECT_FALSE(called);
  // The manager is shutting down, so |SetWhenCalled| is not called when
  // |callback| is called.
  EXPECT_TRUE(executed_callback);

  bool coroutine_started = false;
  manager.StartCoroutine(callback::SetWhenCalled(&called),
                         [&](CoroutineHandler* current_handler, fit::function<void()> callback) {
                           coroutine_started = true;
                           callback();
                         });
  EXPECT_FALSE(called);
  EXPECT_FALSE(coroutine_started);
}

TEST_P(CoroutineManagerTest, NoCallback) {
  CoroutineServiceImpl coroutine_service;
  CoroutineManager manager(&coroutine_service, GetParam());

  CoroutineHandler* handler = nullptr;
  manager.StartCoroutine([&handler](CoroutineHandler* current_handler) {
    handler = current_handler;
    EXPECT_EQ(handler->Yield(), ContinuationStatus::OK);
  });

  ASSERT_TRUE(handler);
  handler->Resume(ContinuationStatus::OK);
}

TEST_P(CoroutineManagerTest, DeleteInCallback) {
  CoroutineServiceImpl coroutine_service;
  std::unique_ptr<CoroutineManager> manager =
      std::make_unique<CoroutineManager>(&coroutine_service, GetParam());

  manager->StartCoroutine(
      [&manager] { manager.reset(); },
      [](CoroutineHandler* current_handler, fit::function<void()> callback) { callback(); });
}

TEST_P(CoroutineManagerTest, DeleteAfterCallback) {
  CoroutineServiceImpl coroutine_service;
  std::unique_ptr<CoroutineManager> manager =
      std::make_unique<CoroutineManager>(&coroutine_service, GetParam());

  manager->StartCoroutine(
      [] {},
      [&manager](CoroutineHandler* current_handler, fit::function<void()> callback) {
        callback();
        manager.reset();
      });
}

INSTANTIATE_TEST_SUITE_P(CoroutineManagerTest, CoroutineManagerTest, testing::Values(0, 1, 2, 100));

// Below, some tests with a controlled maximum number of coroutines checking pending tasks behavior.

TEST(CoroutineManagerTest, DeleteInCallbackMultipleCoroutines) {
  CoroutineServiceImpl coroutine_service;
  std::unique_ptr<CoroutineManager> manager =
      std::make_unique<CoroutineManager>(&coroutine_service, /*max_coroutines=*/2);

  CoroutineHandler* handler = nullptr;
  manager->StartCoroutine(
      [&manager] { manager.reset(); },
      [&handler](CoroutineHandler* current_handler, fit::function<void()> callback) {
        handler = current_handler;
        // This coroutine will be resumed.
        EXPECT_EQ(handler->Yield(), ContinuationStatus::OK);
        callback();
      });
  bool called_2 = false;
  CoroutineHandler* handler_2 = nullptr;
  manager->StartCoroutine(
      callback::SetWhenCalled(&called_2),
      [&handler_2](CoroutineHandler* current_handler, fit::function<void()> callback) {
        handler_2 = current_handler;
        // This coroutine will be cancelled when the manager is deleted.
        EXPECT_EQ(handler_2->Yield(), ContinuationStatus::INTERRUPTED);
        callback();
      });
  bool called_3 = false;
  CoroutineHandler* handler_3 = nullptr;
  manager->StartCoroutine(
      callback::SetWhenCalled(&called_3),
      [&handler_3](CoroutineHandler* current_handler, fit::function<void()> callback) {
        handler_3 = current_handler;
        // This point should never be reached, the coroutine will not even start.
        EXPECT_EQ(handler_3->Yield(), ContinuationStatus::INTERRUPTED);
        callback();
      });
  // The first two tasks are started, the third one waits for a coroutine.
  ASSERT_TRUE(handler);
  EXPECT_TRUE(handler_2);
  EXPECT_FALSE(handler_3);
  // Resume first task, which deletes the manager. The second coroutine should be interrupted (its
  // callback is not called), and the third one never started.
  handler->Resume(ContinuationStatus::OK);
  EXPECT_EQ(manager, nullptr);
  EXPECT_FALSE(called_2);
  EXPECT_EQ(handler_3, nullptr);
  EXPECT_FALSE(called_3);
}

TEST(CoroutineManagerTest, MultipleConcurrentCoroutines) {
  CoroutineServiceImpl coroutine_service;
  CoroutineManager manager(&coroutine_service, 2);

  bool called = false;
  CoroutineHandler* handler = nullptr;
  manager.StartCoroutine(
      callback::SetWhenCalled(&called),
      [&handler](CoroutineHandler* current_handler, fit::function<void()> callback) {
        handler = current_handler;
        EXPECT_EQ(handler->Yield(), ContinuationStatus::OK);
        callback();
      });

  bool called_2 = false;
  CoroutineHandler* handler_2 = nullptr;
  manager.StartCoroutine(
      callback::SetWhenCalled(&called_2),
      [&handler_2](CoroutineHandler* current_handler_2, fit::function<void()> callback) {
        handler_2 = current_handler_2;
        EXPECT_EQ(handler_2->Yield(), ContinuationStatus::OK);
        callback();
      });

  // Both coroutines are started.
  ASSERT_TRUE(handler);
  ASSERT_TRUE(handler_2);
  // Both of them have yielded before completion.
  EXPECT_FALSE(called);
  EXPECT_FALSE(called_2);
  // Resume first coroutine.
  handler->Resume(ContinuationStatus::OK);
  EXPECT_TRUE(called);
  EXPECT_FALSE(called_2);
  // Resume second coroutine.
  handler_2->Resume(ContinuationStatus::OK);
  EXPECT_TRUE(called_2);
}

TEST(CoroutineManagerTest, MultipleSerializedCoroutines) {
  CoroutineServiceImpl coroutine_service;
  // Limit the number of concurrent coroutines to 1 to force them running in sequence.
  CoroutineManager manager(&coroutine_service, 1);

  bool called = false;
  CoroutineHandler* handler = nullptr;
  manager.StartCoroutine(
      callback::SetWhenCalled(&called),
      [&handler](CoroutineHandler* current_handler, fit::function<void()> callback) {
        handler = current_handler;
        EXPECT_EQ(handler->Yield(), ContinuationStatus::OK);
        callback();
      });

  bool called_2 = false;
  CoroutineHandler* handler_2 = nullptr;
  manager.StartCoroutine(
      callback::SetWhenCalled(&called_2),
      [&handler_2](CoroutineHandler* current_handler_2, fit::function<void()> callback) {
        handler_2 = current_handler_2;
        EXPECT_EQ(handler_2->Yield(), ContinuationStatus::OK);
        callback();
      });

  // The first task is running in a coroutine, but has not completed yet.
  ASSERT_TRUE(handler);
  EXPECT_FALSE(called);
  // The second task is enqueued, waiting for a coroutine to run.
  EXPECT_EQ(handler_2, nullptr);
  EXPECT_FALSE(called_2);
  // Resume the coroutine, completing first task and starting the second one.
  handler->Resume(ContinuationStatus::OK);
  EXPECT_TRUE(called);
  ASSERT_TRUE(handler_2);
  EXPECT_FALSE(called_2);
  // Complete the second task.
  handler_2->Resume(ContinuationStatus::OK);
  EXPECT_TRUE(called_2);

  // Start a third task.
  bool called_3 = false;
  CoroutineHandler* handler_3 = nullptr;
  manager.StartCoroutine(
      callback::SetWhenCalled(&called_3),
      [&handler_3](CoroutineHandler* current_handler_3, fit::function<void()> callback) {
        handler_3 = current_handler_3;
        EXPECT_EQ(handler_3->Yield(), ContinuationStatus::OK);
        callback();
      });

  ASSERT_TRUE(handler_3);
  EXPECT_FALSE(called_3);
  handler_3->Resume(ContinuationStatus::OK);
  EXPECT_TRUE(called_3);
}

// Checks that if a coroutine is interrupted, other coroutines can still run on the manager.
TEST(CoroutineManagerTest, MultipleCoroutinesInterrupted) {
  CoroutineServiceImpl coroutine_service;
  // Limit the number of concurrent coroutines to 1 to force them running in sequence.
  CoroutineManager manager(&coroutine_service, 1);

  bool called = false;
  CoroutineHandler* handler = nullptr;
  manager.StartCoroutine(
      callback::SetWhenCalled(&called),
      [&handler](CoroutineHandler* current_handler, fit::function<void()> callback) {
        handler = current_handler;
        EXPECT_EQ(handler->Yield(), ContinuationStatus::INTERRUPTED);
        callback();
      });

  bool called_2 = false;
  CoroutineHandler* handler_2 = nullptr;
  manager.StartCoroutine(
      callback::SetWhenCalled(&called_2),
      [&handler_2](CoroutineHandler* current_handler_2, fit::function<void()> callback) {
        handler_2 = current_handler_2;
        EXPECT_EQ(handler_2->Yield(), ContinuationStatus::OK);
        callback();
      });

  // The first task is running in a coroutine, but has not completed yet.
  ASSERT_TRUE(handler);
  EXPECT_FALSE(called);
  // The second task is enqueued, waiting for a coroutine to run.
  EXPECT_EQ(handler_2, nullptr);
  EXPECT_FALSE(called_2);
  // Resume the coroutine, completing first task and starting the second one.
  handler->Resume(ContinuationStatus::INTERRUPTED);
  EXPECT_TRUE(called);
  ASSERT_TRUE(handler_2);
  EXPECT_FALSE(called_2);
  // Complete the second task.
  handler_2->Resume(ContinuationStatus::OK);
  EXPECT_TRUE(called_2);
}

TEST_P(CoroutineManagerTest, UseSynchronousCoroutineHandlerWithOneArgument) {
  CoroutineServiceImpl coroutine_service;
  CoroutineManager manager(&coroutine_service, GetParam());

  bool called = false;
  int value;
  manager.StartCoroutine(callback::Capture(callback::SetWhenCalled(&called), &value),
                         [](CoroutineHandler* current_handler) { return 1; });

  EXPECT_TRUE(called);
  EXPECT_EQ(value, 1);
}

TEST_P(CoroutineManagerTest, UseSynchronousCoroutineHandlerWithTwoArguments) {
  CoroutineServiceImpl coroutine_service;
  CoroutineManager manager(&coroutine_service, GetParam());

  bool called = false;
  int value1, value2;
  manager.StartCoroutine(callback::Capture(callback::SetWhenCalled(&called), &value1, &value2),
                         [](CoroutineHandler* current_handler) { return std::make_tuple(1, 2); });

  EXPECT_TRUE(called);
  EXPECT_EQ(value1, 1);
  EXPECT_EQ(value2, 2);
}

}  // namespace
}  // namespace coroutine
