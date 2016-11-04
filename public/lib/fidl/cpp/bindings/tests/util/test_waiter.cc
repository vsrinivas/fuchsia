// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a simple async waiter implementation meant to be used by tests and
// exposed using GetDefaultAsyncWaiter(). It also includes a
// WaitForAsyncWaiter() and ClearAsyncWaiter() functions for pumping messages
// from handles and clearing them.
// TODO(vardhan): Make this AsyncWaiter impl use thread-local storage so that
// tests using multiple threads can work.

#include <unordered_set>

#include <mx/waitset.h>

#include "lib/fidl/cpp/bindings/tests/util/test_waiter.h"
#include "lib/fidl/cpp/waiter/default.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/arraysize.h"

namespace fidl {
namespace {

struct WaitHolder {
  FidlAsyncWaitCallback callback;
  void* context;
};

// TODO(vardhan): Probably shouldn't have global objects with non-trivial
// constructors. Clean this up.
static std::unordered_set<struct WaitHolder*> g_all_holders;
static mx::waitset* g_waiting_set = nullptr;

// This implementation of AsyncWait completely disregards the supplied timeout.
FidlAsyncWaitID AsyncWait(mx_handle_t handle,
                          mx_signals_t signals,
                          mx_time_t /* timeout */,
                          FidlAsyncWaitCallback callback,
                          void* context) {
  FTL_CHECK(g_waiting_set);
  struct WaitHolder* holder = new WaitHolder{callback, context};
  auto result = g_waiting_set->add(reinterpret_cast<uint64_t>(holder), handle,
                                   signals);
  FTL_CHECK(result == NO_ERROR);
  g_all_holders.insert(holder);
  return reinterpret_cast<FidlAsyncWaitID>(holder);
}

void CancelWait(FidlAsyncWaitID wait_id) {
  FTL_DCHECK(g_waiting_set);
  auto result = g_waiting_set->remove(reinterpret_cast<uint64_t>(wait_id));
  FTL_CHECK(result == NO_ERROR);
  auto* holder = reinterpret_cast<WaitHolder*>(wait_id);
  g_all_holders.erase(holder);
  delete holder;
}

static constexpr FidlAsyncWaiter kDefaultAsyncWaiter = {AsyncWait, CancelWait};

}  // namespace

namespace test {

void WaitForAsyncWaiter() {
  mx_waitset_result_t results[10];
  mx_status_t result = NO_ERROR;
  uint32_t num_results = arraysize(results);
  // TODO(vardhan): Once mx_waitset_wait() has been fixed, update our usage
  // here.
  result = g_waiting_set->wait(0, results, &num_results);
  if (result == NO_ERROR) {
    if (num_results == 0)
      return;
    for (uint32_t i = 0; i < num_results; i++) {
      // It is important to remove this handle from our waitset /before/
      // dispatching the callback.
      auto* holder = reinterpret_cast<struct WaitHolder*>(results[i].cookie);
      auto cb = holder->callback;
      auto context = holder->context;
      mx_waitset_result_t result = results[i];
      CancelWait(reinterpret_cast<FidlAsyncWaitID>(holder));
      cb(result.status, result.observed, context);
    }
    WaitForAsyncWaiter();
  } else {
    FTL_CHECK(result == ERR_TIMED_OUT) << result;
  }
}

void ClearAsyncWaiter() {
  for (auto* holder : g_all_holders) {
    auto result = g_waiting_set->remove(reinterpret_cast<uint64_t>(holder));
    FTL_CHECK(result == NO_ERROR) << result;
    delete holder;
  }
  g_all_holders.clear();
}

}  // namespace test

// Not thread safe.
const FidlAsyncWaiter* GetDefaultAsyncWaiter() {
  if (!g_waiting_set) {
    g_waiting_set = new mx::waitset;
    auto result = mx::waitset::create(0, g_waiting_set);
    FTL_CHECK(result == NO_ERROR) << result;
  }
  return &kDefaultAsyncWaiter;
}

}  // namespace fidl
