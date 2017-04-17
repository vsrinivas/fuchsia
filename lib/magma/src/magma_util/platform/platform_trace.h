// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_TRACE_H
#define PLATFORM_TRACE_H

#if MAGMA_ENABLE_TRACING
#include "apps/tracing/lib/trace/event.h" // nogncheck
#define TRACE_NONCE_DECLARE(x) uint64_t x = TRACE_NONCE()
#else
#define TRACE_NONCE() 0
#define TRACE_NONCE_DECLARE(x)
#define TRACE_ASYNC_BEGIN(category, name, id, args...)
#define TRACE_ASYNC_END(category, name, id, args...)
#endif

namespace magma {

class PlatformTrace {
public:
    static void Initialize();

    static PlatformTrace* GetInstance();
};

} // namespace magma

#endif // PLATFORM_TRACE_H
