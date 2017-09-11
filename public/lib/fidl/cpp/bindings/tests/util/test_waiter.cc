// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a simple async waiter implementation meant to be used by tests and
// exposed using GetDefaultAsyncWaiter(). It also includes a
// WaitForAsyncWaiter() and ClearAsyncWaiter() functions for pumping messages
// from handles and clearing them.
// TODO(vardhan): Make this AsyncWaiter impl use thread-local storage so that
// tests using multiple threads can work.

#include <map>

#include <magenta/syscalls/port.h>
#include <mx/port.h>

#include "lib/fidl/cpp/bindings/tests/util/test_waiter.h"
#include "lib/fidl/cpp/waiter/default.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/arraysize.h"

namespace fidl {
namespace {

struct WaitHolder {
  mx_handle_t handle;
  FidlAsyncWaitCallback callback;
  void* context;
};

// TODO(vardhan): Probably shouldn't have global objects with non-trivial
// constructors. Clean this up.
static FidlAsyncWaitID g_next_key = 1;
static std::map<uint64_t, struct WaitHolder*> g_holders;
static mx::port* g_port = nullptr;

// This implementation of AsyncWait completely disregards the supplied timeout.
FidlAsyncWaitID AsyncWait(mx_handle_t handle,
                          mx_signals_t signals,
                          mx_time_t /* timeout */,
                          FidlAsyncWaitCallback callback,
                          void* context) {
  FXL_CHECK(g_port);
  FidlAsyncWaitID wait_id = g_next_key++;
  struct WaitHolder* holder = new WaitHolder{handle, callback, context};
  auto result = mx_object_wait_async(handle, g_port->get(), wait_id, signals,
                                     MX_WAIT_ASYNC_ONCE);
  FXL_CHECK(result == MX_OK);
  g_holders.emplace(wait_id, holder);
  return wait_id;
}

void CancelWait(FidlAsyncWaitID wait_id) {
  FXL_DCHECK(g_port);
  auto* holder = g_holders[wait_id];
  auto result = g_port->cancel(holder->handle, wait_id);
  FXL_CHECK(result == MX_OK);
  g_holders.erase(wait_id);
  delete holder;
}

static constexpr FidlAsyncWaiter kDefaultAsyncWaiter = {AsyncWait, CancelWait};

}  // namespace

namespace test {

void WaitForAsyncWaiter() {
  while (!g_holders.empty()) {
    mx_port_packet_t packet;
    mx_status_t result = g_port->wait(0, &packet, 0);
    if (result == MX_OK) {
      FXL_CHECK(packet.type == MX_PKT_TYPE_SIGNAL_ONE) << packet.type;
      FXL_CHECK(packet.status == MX_OK) << packet.status;
      FidlAsyncWaitID wait_id = packet.key;

      // This wait was already canceled. TODO(cpu): Remove once canceled waits
      // don't trigger packets any more.
      if (g_holders.find(wait_id) == g_holders.end())
        continue;

      auto* holder = g_holders[wait_id];
      auto cb = holder->callback;
      auto context = holder->context;
      g_holders.erase(wait_id);
      delete holder;
      cb(packet.status, packet.signal.observed, 1, context);
    } else {
      FXL_CHECK(result == MX_ERR_TIMED_OUT) << result;
      return;
    }
  }
}

void ClearAsyncWaiter() {
  for (const auto& entry : g_holders) {
    FidlAsyncWaitID wait_id = entry.first;
    auto* holder = entry.second;
    auto result = g_port->cancel(holder->handle, wait_id);
    FXL_CHECK(result == MX_OK) << result;
    delete holder;
  }
  g_holders.clear();
}

}  // namespace test

// Not thread safe.
const FidlAsyncWaiter* GetDefaultAsyncWaiter() {
  if (!g_port) {
    g_port = new mx::port;
    auto result = mx::port::create(0, g_port);
    FXL_CHECK(result == MX_OK) << result;
  }
  return &kDefaultAsyncWaiter;
}

}  // namespace fidl
