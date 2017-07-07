// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_TRACE_H
#define PLATFORM_TRACE_H

#if MAGMA_ENABLE_TRACING
#include "apps/tracing/lib/trace/event.h"    //nogncheck
#include "apps/tracing/lib/trace/provider.h" //nogncheck
#define TRACE_NONCE_DECLARE(x) uint64_t x = TRACE_NONCE()
#else
#define TRACE_NONCE() 0
#define TRACE_NONCE_DECLARE(x)
#define TRACE_ASYNC_BEGIN(category, name, id, args...)
#define TRACE_ASYNC_END(category, name, id, args...)
#define TRACE_SCOPE_GLOBAL 0
#define TRACE_INSTANT(category, name, id, args...)
#define TRACE_DURATION(category, name, args...)
#define TRACE_DURATION_BEGIN(category, name, args...)
#define TRACE_DURATION_END(category, name, args...)
#define TRACE_FLOW_BEGIN(category, name, id, args...)
#define TRACE_FLOW_STEP(category, name, id, args...)
#define TRACE_FLOW_END(category, name, id, args...)
#endif

namespace magma {

class PlatformTrace {
public:
    static void Initialize();
};

} // namespace magma

#endif // PLATFORM_TRACE_H
