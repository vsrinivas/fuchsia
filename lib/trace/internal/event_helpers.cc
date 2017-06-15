// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace/internal/event_helpers.h"

#include <atomic>

namespace tracing {
namespace internal {

namespace {
std::atomic<uint64_t> g_nonce = ATOMIC_VAR_INIT(1u);
}  // namespace

uint64_t TraceNonce() {
  return g_nonce.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace internal
}  // namespace tracing
