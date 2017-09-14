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

#include <zircon/syscalls/port.h>
#include <zx/port.h>

#include "lib/fidl/cpp/bindings/tests/util/test_waiter.h"
#include "lib/fidl/cpp/waiter/default.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/arraysize.h"

namespace fidl {
namespace {

struct WaitHolder {
  zx_handle_t handle;
  FidlAsyncWaitCallback callback;
  void* context;
};

// TODO(vardhan): Probably shouldn't have global objects with non-trivial
// constructors. Clean this up.
static FidlAsyncWaitID g_next_key = 1;
static std::map<uint64_t, struct WaitHolder*> g_holders;
static zx::port* g_port = nullptr;

// This implementation of AsyncWait completely disregards the supplied timeout.
FidlAsyncWaitID AsyncWait(zx_handle_t handle,
                          zx_signals_t signals,
                          zx_time_t /* timeout */,
                          FidlAsyncWaitCallback callback,
                          void* context) {
  FXL_CHECK(g_port);
  FidlAsyncWaitID wait_id = g_next_key++;
  struct WaitHolder* holder = new WaitHolder{handle, callback, context};
  auto result = zx_object_wait_async(handle, g_port->get(), wait_id, signals,
                                     ZX_WAIT_ASYNC_ONCE);
  FXL_CHECK(result == ZX_OK);
  g_holders.emplace(wait_id, holder);
  return wait_id;
}

void CancelWait(FidlAsyncWaitID wait_id) {
  FXL_DCHECK(g_port);
  auto* holder = g_holders[wait_id];
  auto result = g_port->cancel(holder->handle, wait_id);
  FXL_CHECK(result == ZX_OK);
  g_holders.erase(wait_id);
  delete holder;
}

static constexpr FidlAsyncWaiter kDefaultAsyncWaiter = {AsyncWait, CancelWait};

}  // namespace

namespace test {

void WaitForAsyncWaiter() {
  while (!g_holders.empty()) {
    zx_port_packet_t packet;
    zx_status_t result = g_port->wait(0, &packet, 0);
    if (result == ZX_OK) {
      FXL_CHECK(packet.type == ZX_PKT_TYPE_SIGNAL_ONE) << packet.type;
      FXL_CHECK(packet.status == ZX_OK) << packet.status;
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
      FXL_CHECK(result == ZX_ERR_TIMED_OUT) << result;
      return;
    }
  }
}

void ClearAsyncWaiter() {
  for (const auto& entry : g_holders) {
    FidlAsyncWaitID wait_id = entry.first;
    auto* holder = entry.second;
    auto result = g_port->cancel(holder->handle, wait_id);
    FXL_CHECK(result == ZX_OK) << result;
    delete holder;
  }
  g_holders.clear();
}

}  // namespace test

// Not thread safe.
const FidlAsyncWaiter* GetDefaultAsyncWaiter() {
  if (!g_port) {
    g_port = new zx::port;
    auto result = zx::port::create(0, g_port);
    FXL_CHECK(result == ZX_OK) << result;
  }
  return &kDefaultAsyncWaiter;
}

}  // namespace fidl
