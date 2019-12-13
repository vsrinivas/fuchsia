// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/coroutine/coroutine_waiter.h"

#include <lib/fit/function.h>

#include "gtest/gtest.h"
#include "src/ledger/lib/callback/waiter.h"
#include "src/ledger/lib/coroutine/coroutine_impl.h"
#include "src/ledger/lib/memory/ref_ptr.h"

namespace coroutine {
namespace {
TEST(CoroutineWaiterTest, Wait) {
  CoroutineServiceImpl coroutine_service;

  fit::closure on_done;
  coroutine_service.StartCoroutine([&](CoroutineHandler* current_handler) {
    auto waiter = ledger::MakeRefCounted<ledger::CompletionWaiter>();
    on_done = waiter->NewCallback();
    EXPECT_EQ(Wait(current_handler, std::move(waiter)), ContinuationStatus::OK);
  });

  on_done();
}

TEST(CoroutineWaiterTest, Cancel) {
  CoroutineServiceImpl coroutine_service;
  CoroutineHandler* coroutine_handler;

  fit::closure scoped_callback;
  coroutine_service.StartCoroutine([&](CoroutineHandler* handler) {
    coroutine_handler = handler;
    auto waiter = ledger::MakeRefCounted<ledger::CompletionWaiter>();
    scoped_callback = waiter->MakeScoped(
        [callback = waiter->NewCallback()] { FAIL() << "Should not be called"; });
    EXPECT_EQ(Wait(handler, std::move(waiter)), ContinuationStatus::INTERRUPTED);
  });
  coroutine_handler->Resume(ContinuationStatus::INTERRUPTED);
  scoped_callback();
}
}  // namespace
}  // namespace coroutine
