// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/functional/make_copyable.h"
#include "peridot/bin/ledger/coroutine/coroutine_impl.h"

namespace coroutine {
namespace {

size_t Fact(size_t n) {
  if (n == 0) {
    return 1;
  }
  return Fact(n - 1) * n;
}

void UseStack() { EXPECT_EQ(120u, Fact(5)); }

TEST(Coroutine, SingleRoutine) {
  CoroutineServiceImpl coroutine_service;

  CoroutineHandler* handler = nullptr;
  constexpr int kLoopCount = 10;
  int result = kLoopCount;

  coroutine_service.StartCoroutine(
      [&handler, &result](CoroutineHandler* current_handler) {
        handler = current_handler;
        UseStack();
        do {
          EXPECT_EQ(ContinuationStatus::OK, current_handler->Yield());
          UseStack();
          --result;
        } while (result);
      });

  EXPECT_TRUE(handler);
  EXPECT_EQ(kLoopCount, result);

  for (int i = kLoopCount - 1; i >= 0; --i) {
    handler->Resume(ContinuationStatus::OK);
    EXPECT_EQ(i, result);
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
        EXPECT_EQ(ContinuationStatus::OK, handler->Yield());
        UseStack();
      }

      handlers.erase(handlers.find(handler));
    });
  }

  EXPECT_EQ(nb_routines, handlers.size());

  for (size_t i = 0; i < 2; ++i) {
    for (CoroutineHandler* handler : handlers) {
      handler->Resume(ContinuationStatus::OK);
    }
  }

  EXPECT_EQ(nb_routines, handlers.size());

  for (size_t i = 0; i < nb_routines; ++i) {
    (*handlers.begin())->Resume(ContinuationStatus::OK);
  }

  EXPECT_TRUE(handlers.empty());
}

TEST(Coroutine, AsyncCall) {
  CoroutineServiceImpl coroutine_service;

  std::function<void(size_t)> callback;
  auto callable = [&callback](std::function<void(size_t)> called_callback) {
    callback = std::move(called_callback);
  };

  size_t received_value = 0;
  coroutine_service.StartCoroutine(
      [callable, &received_value](CoroutineHandler* handler) {
        UseStack();
        size_t value;
        EXPECT_EQ(ContinuationStatus::OK, SyncCall(handler, callable, &value));
        UseStack();
        received_value = value;
      });

  EXPECT_TRUE(callback);
  EXPECT_EQ(0u, received_value);

  callback(1);

  EXPECT_EQ(1u, received_value);
}

TEST(Coroutine, SynchronousAsyncCall) {
  CoroutineServiceImpl coroutine_service;

  size_t received_value = 0;
  coroutine_service.StartCoroutine(
      [&received_value](CoroutineHandler* handler) {
        UseStack();
        EXPECT_EQ(
            ContinuationStatus::OK,
            SyncCall(handler,
                     [](std::function<void(size_t)> callback) { callback(1); },
                     &received_value));
        UseStack();
      });
  EXPECT_EQ(1u, received_value);
}

TEST(Coroutine, DroppedAsyncCall) {
  CoroutineServiceImpl coroutine_service;

  bool ended = false;
  coroutine_service.StartCoroutine([&ended](CoroutineHandler* handler) {
    EXPECT_EQ(ContinuationStatus::INTERRUPTED,
              SyncCall(handler, [](std::function<void()> callback) {
                // |callback| is dropped here.
              }));
    ended = true;
  });
  EXPECT_TRUE(ended);
}

TEST(Coroutine, DroppedAsyncCallAsynchronously) {
  CoroutineServiceImpl coroutine_service;

  bool ended = false;
  std::function<void()> callback;

  coroutine_service.StartCoroutine([&ended,
                                    &callback](CoroutineHandler* handler) {
    EXPECT_EQ(
        ContinuationStatus::INTERRUPTED,
        SyncCall(handler, [&callback](std::function<void()> received_callback) {
          callback = received_callback;
        }));
    ended = true;
  });

  EXPECT_FALSE(ended);
  EXPECT_TRUE(callback);
  callback = [] {};

  EXPECT_TRUE(ended);
}

TEST(Coroutine, RunAndDroppedAsyncCallAfterCoroutineDeletion) {
  bool ended = false;
  std::function<void()> callback;
  {
    CoroutineServiceImpl coroutine_service;

    coroutine_service.StartCoroutine([&ended,
                                      &callback](CoroutineHandler* handler) {
      EXPECT_EQ(ContinuationStatus::INTERRUPTED,
                SyncCall(handler,
                         [&callback](std::function<void()> received_callback) {
                           callback = received_callback;
                         }));
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

    EXPECT_EQ(ContinuationStatus::OK, status);
  }

  EXPECT_EQ(ContinuationStatus::INTERRUPTED, status);
}

TEST(Coroutine, ReuseStack) {
  CoroutineServiceImpl coroutine_service;
  CoroutineHandler* handler = nullptr;
  uintptr_t stack_pointer = 0;
  size_t nb_coroutines_calls = 0;

  for (size_t i = 0; i < 2; ++i) {
    coroutine_service.StartCoroutine(
        [&handler, &stack_pointer,
         &nb_coroutines_calls](CoroutineHandler* called_handler) {
          UseStack();
          int a;
          uintptr_t addr = reinterpret_cast<uintptr_t>(&a);
          if (stack_pointer == 0) {
            stack_pointer = addr;
          }
          EXPECT_EQ(addr, stack_pointer);
          handler = called_handler;
          EXPECT_EQ(ContinuationStatus::OK, called_handler->Yield());
          UseStack();

          ++nb_coroutines_calls;
        });
    handler->Resume(ContinuationStatus::OK);
  }

  EXPECT_EQ(2u, nb_coroutines_calls);
}

TEST(Coroutine, ResumeCoroutineInOtherCoroutineDestructor) {
  CoroutineServiceImpl coroutine_service;
  CoroutineHandler* handler1 = nullptr;
  CoroutineHandler* handler2 = nullptr;
  bool routine1_done = false;
  bool routine2_done = false;

  coroutine_service.StartCoroutine([&](CoroutineHandler* local_handler1) {
    handler1 = local_handler1;
    auto autocall =
        fxl::MakeAutoCall([&] { handler1->Resume(ContinuationStatus::OK); });
    coroutine_service.StartCoroutine(fxl::MakeCopyable(
        [&handler2, &routine2_done,
         autocall = std::move(autocall)](CoroutineHandler* local_handler2) {
          handler2 = local_handler2;
          EXPECT_EQ(ContinuationStatus::OK, handler2->Yield());
          routine2_done = true;
        }));
    EXPECT_EQ(ContinuationStatus::OK, handler1->Yield());
    routine1_done = true;
  });

  handler2->Resume(ContinuationStatus::OK);

  EXPECT_TRUE(routine1_done);
  EXPECT_TRUE(routine2_done);
}

TEST(Coroutine, AsyncCallCapture) {
  CoroutineServiceImpl coroutine_service;

  std::function<void(size_t)> callback;
  auto callable = [&callback](std::function<void(size_t)> called_callback) {
    callback = std::move(called_callback);
  };

  size_t value = 0;
  CoroutineHandler* coroutine_handler;
  coroutine_service.StartCoroutine(
      [callable, &coroutine_handler, &value](CoroutineHandler* handler) {
        coroutine_handler = handler;
        EXPECT_EQ(ContinuationStatus::INTERRUPTED,
                  SyncCall(handler, callable, &value));
        return;
      });

  EXPECT_TRUE(callback);

  coroutine_handler->Resume(ContinuationStatus::INTERRUPTED);

  callback(10);

  EXPECT_EQ(0u, value);
}

}  // namespace
}  // namespace coroutine
