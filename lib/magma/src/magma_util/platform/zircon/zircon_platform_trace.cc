// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_trace.h"

#include <memory>

#include "magma_util/dlog.h"
#include "magma_util/macros.h"

namespace magma {

#if MAGMA_ENABLE_TRACING

static std::unique_ptr<ZirconPlatformTrace> g_platform_trace;

ZirconPlatformTrace::ZirconPlatformTrace()
    : loop_(&kAsyncLoopConfigNoAttachToThread),
      trace_provider_(loop_.dispatcher()) {}

bool ZirconPlatformTrace::Initialize()
{
    zx_status_t status = loop_.StartThread();
    if (status != ZX_OK)
        return DRETF(false, "Failed to start async loop");
    return true;
}

void ZirconPlatformTrace::SetObserver(std::function<void(bool)> callback)
{
    observer_.Stop();
    enabled_ = false;

    observer_.Start(loop_.dispatcher(), [this, callback] {
        bool enabled = trace_state() == TRACE_STARTED;
        if (this->enabled_ != enabled) {
            this->enabled_ = enabled;
            callback(enabled);
        }
    });
}

PlatformTrace* PlatformTrace::Get()
{
    if (!g_platform_trace)
        g_platform_trace = std::make_unique<ZirconPlatformTrace>();
    return g_platform_trace.get();
}

#else

PlatformTrace* PlatformTrace::Get() { return nullptr; }

#endif


} // namespace magma
