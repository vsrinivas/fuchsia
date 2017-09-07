// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trace-engine/instrumentation.h>

#include <fbl/atomic.h>

namespace {

fbl::atomic_uint64_t g_nonce{1u};

} // namespace

uint64_t trace_generate_nonce() {
    return g_nonce.fetch_add(1u, fbl::memory_order_relaxed);
}
