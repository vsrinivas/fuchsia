// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/lib/coroutine/coroutine_impl.h"
#include "src/ledger/lib/logging/logging.h"

namespace coroutine {
namespace {

size_t Fact(size_t n) {
  if (n == 0) {
    return 1;
  }
  return Fact(n - 1) * n;
}

void UseStack() { EXPECT_EQ(Fact(5), 120u); }

TEST(Coroutine, SingleRoutine) {
  CoroutineServiceImpl coroutine_service;

  CoroutineHandler* handler = nullptr;
  constexpr int kLoopCount = 10;
  int result = kLoopCount;

  coroutine_service.StartCoroutine([&handler, &result](CoroutineHandler* current_handler) {
    handler = current_handler;
    UseStack();
    do {
      EXPECT_EQ(current_handler->Yield(), ContinuationStatus::OK);
      UseStack();
      --result;
    } while (result);
  });

  EXPECT_TRUE(handler);
  EXPECT_EQ(result, kLoopCount);

  for (int i = kLoopCount - 1; i >= 0; --i) {
    handler->Resume(ContinuationStatus::OK);
    EXPECT_EQ(result, i);
  }
}

TEST(Coroutine, ManyRoutines) {
  constexpr size_t nb_routines = 1000;

  CoroutineServiceImpl coroutine_service;

  std::set<CoroutineHandler*> handlers;

  for (size_t i = 0; i < nb_routines; ++i) {
    coroutine_service.StartCoroutine([&handlers](CoroutineHandler* handler) {
      handlers.insert(handler);
      UseStack();

      for (size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(handler->Yield(), ContinuationStatus::OK);
        UseStack();
      }

      handlers.erase(handlers.find(handler));
    });
  }

  EXPECT_EQ(handlers.size(), nb_routines);

  for (size_t i = 0; i < 2; ++i) {
    for (CoroutineHandler* handler : handlers) {
      handler->Resume(ContinuationStatus::OK);
    }
  }

  EXPECT_EQ(handlers.size(), nb_routines);

  for (size_t i = 0; i < nb_routines; ++i) {
    (*handlers.begin())->Resume(ContinuationStatus::OK);
  }

  EXPECT_TRUE(handlers.empty());
}

TEST(Coroutine, AsyncCall) {
  CoroutineServiceImpl coroutine_service;

  fit::function<void(size_t)> callback;
  auto callable = [&callback](fit::function<void(size_t)> called_callback) {
    callback = std::move(called_callback);
  };

  size_t received_value = 0;
  coroutine_service.StartCoroutine([callable, &received_value](CoroutineHandler* handler) {
    UseStack();
    size_t value;
    EXPECT_EQ(SyncCall(handler, callable, &value), ContinuationStatus::OK);
    UseStack();
    received_value = value;
  });

  EXPECT_TRUE(callback);
  EXPECT_EQ(received_value, 0u);

  callback(1);

  EXPECT_EQ(received_value, 1u);
}

TEST(Coroutine, SynchronousAsyncCall) {
  CoroutineServiceImpl coroutine_service;

  size_t received_value = 0;
  coroutine_service.StartCoroutine([&received_value](CoroutineHandler* handler) {
    UseStack();
    EXPECT_EQ(
        SyncCall(
            handler, [](fit::function<void(size_t)> callback) { callback(1); }, &received_value),
        ContinuationStatus::OK);
    UseStack();
  });
  EXPECT_EQ(received_value, 1u);
}

TEST(Coroutine, DroppedAsyncCall) {
  CoroutineServiceImpl coroutine_service;

  bool ended = false;
  coroutine_service.StartCoroutine([&ended](CoroutineHandler* handler) {
    EXPECT_EQ(SyncCall(handler,
                       [](fit::function<void()> callback) {
                         // |callback| is dropped here.
                       }),
              ContinuationStatus::INTERRUPTED);
    ended = true;
  });
  EXPECT_TRUE(ended);
}

TEST(Coroutine, DroppedAsyncCallAsynchronously) {
  CoroutineServiceImpl coroutine_service;

  bool ended = false;
  fit::function<void()> callback;

  coroutine_service.StartCoroutine([&ended, &callback](CoroutineHandler* handler) {
    EXPECT_EQ(SyncCall(handler,
                       [&callback](fit::function<void()> received_callback) {
                         callback = std::move(received_callback);
                       }),
              ContinuationStatus::INTERRUPTED);
    ended = true;
  });

  EXPECT_FALSE(ended);
  EXPECT_TRUE(callback);
  callback = [] {};

  EXPECT_TRUE(ended);
}

TEST(Coroutine, RunAndDroppedAsyncCallAfterCoroutineDeletion) {
  bool ended = false;
  fit::function<void()> callback;
  {
    CoroutineServiceImpl coroutine_service;

    coroutine_service.StartCoroutine([&ended, &callback](CoroutineHandler* handler) {
      EXPECT_EQ(SyncCall(handler,
                         [&callback](fit::function<void()> received_callback) {
                           callback = std::move(received_callback);
                         }),
                ContinuationStatus::INTERRUPTED);
      ended = true;
    });

    EXPECT_FALSE(ended);
    EXPECT_TRUE(callback);
  }

  EXPECT_TRUE(ended);
  callback();
}

TEST(Coroutine, Interrupt) {
  ContinuationStatus status = ContinuationStatus::OK;

  {
    CoroutineServiceImpl coroutine_service;

    coroutine_service.StartCoroutine([&status](CoroutineHandler* handler) {
      UseStack();
      status = handler->Yield();
      UseStack();
    });

    EXPECT_EQ(status, ContinuationStatus::OK);
  }

  EXPECT_EQ(status, ContinuationStatus::INTERRUPTED);
}

#if !__has_feature(address_sanitizer)
TEST(Coroutine, ReuseStack) {
  CoroutineServiceImpl coroutine_service;
  CoroutineHandler* handler = nullptr;
  uintptr_t stack_pointer = 0;
  size_t nb_coroutines_calls = 0;

  for (size_t i = 0; i < 2; ++i) {
    coroutine_service.StartCoroutine(
        [&handler, &stack_pointer, &nb_coroutines_calls](CoroutineHandler* called_handler) {
          UseStack();
          int a;
          uintptr_t addr = reinterpret_cast<uintptr_t>(&a);
          if (stack_pointer == 0) {
            stack_pointer = addr;
          }
          EXPECT_EQ(addr, stack_pointer);
          handler = called_handler;
          EXPECT_EQ(called_handler->Yield(), ContinuationStatus::OK);
          UseStack();

          ++nb_coroutines_calls;
        });
    handler->Resume(ContinuationStatus::OK);
  }

  EXPECT_EQ(nb_coroutines_calls, 2u);
}
#endif  // !__has_feature(address_sanitizer)

TEST(Coroutine, ResumeCoroutineInOtherCoroutineDestructor) {
  CoroutineServiceImpl coroutine_service;
  CoroutineHandler* handler1 = nullptr;
  CoroutineHandler* handler2 = nullptr;
  bool routine1_done = false;
  bool routine2_done = false;

  coroutine_service.StartCoroutine([&](CoroutineHandler* local_handler1) {
    handler1 = local_handler1;
    auto autocall = fit::defer([&] { handler1->Resume(ContinuationStatus::OK); });
    coroutine_service.StartCoroutine([&handler2, &routine2_done, autocall = std::move(autocall)](
                                         CoroutineHandler* local_handler2) {
      handler2 = local_handler2;
      EXPECT_EQ(handler2->Yield(), ContinuationStatus::OK);
      routine2_done = true;
    });
    EXPECT_EQ(handler1->Yield(), ContinuationStatus::OK);
    routine1_done = true;
  });

  handler2->Resume(ContinuationStatus::OK);

  EXPECT_TRUE(routine1_done);
  EXPECT_TRUE(routine2_done);
}

TEST(Coroutine, AsyncCallCapture) {
  CoroutineServiceImpl coroutine_service;

  fit::function<void(size_t)> callback;
  auto callable = [&callback](fit::function<void(size_t)> called_callback) {
    callback = std::move(called_callback);
  };

  size_t value = 0;
  CoroutineHandler* coroutine_handler;
  coroutine_service.StartCoroutine(
      [callable, &coroutine_handler, &value](CoroutineHandler* handler) {
        coroutine_handler = handler;
        EXPECT_EQ(SyncCall(handler, callable, &value), ContinuationStatus::INTERRUPTED);
        return;
      });

  EXPECT_TRUE(callback);

  coroutine_handler->Resume(ContinuationStatus::INTERRUPTED);

  callback(10);

  EXPECT_EQ(value, 0u);
}

TEST(Coroutine, DoubleResume) {
  // This tests the existence of a debug assertion.
  // Skip this test when debug assertions are not enabled.
  bool running_debug = false;
  LEDGER_DCHECK(running_debug = true);
  if (!running_debug) {
    GTEST_SKIP();
  }

  CoroutineServiceImpl coroutine_service;

  ASSERT_DEATH(
      {
        coroutine_service.StartCoroutine(
            [](CoroutineHandler* handler) { handler->Resume(ContinuationStatus::INTERRUPTED); });
      },
      "Attempting to resume a running coroutine");
}

TEST(Coroutine, DoubleYield) {
  // This tests the existence of a debug assertion.
  // Skip this test when debug assertions are not enabled.
  bool running_debug = false;
  LEDGER_DCHECK(running_debug = true);
  if (!running_debug) {
    GTEST_SKIP();
  }

  CoroutineServiceImpl coroutine_service;

  CoroutineHandler* coroutine_handler;
  ASSERT_DEATH(
      {
        coroutine_service.StartCoroutine([&coroutine_handler](CoroutineHandler* handler) {
          coroutine_handler = handler;
          (void)handler->Yield();
        });
        (void)coroutine_handler->Yield();
      },
      "Attempting to yield from outside the coroutine");
}

}  // namespace
}  // namespace coroutine
