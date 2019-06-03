// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_PLATFORM_TRACE_H
#define ZIRCON_PLATFORM_TRACE_H

#include <lib/fit/function.h>

#if MAGMA_ENABLE_TRACING
#include <lib/async-loop/cpp/loop.h>
#include <trace-provider/provider.h>
#include <trace/observer.h>
#endif

#include "platform_trace.h"
#include <memory>
#include <vector>

namespace magma {

#if MAGMA_ENABLE_TRACING
class ZirconPlatformTrace : public PlatformTrace {
public:
    ZirconPlatformTrace();

    bool Initialize() override;

private:
    async::Loop loop_;
    trace::TraceProviderWithFdio trace_provider_;
};

class ZirconPlatformTraceObserver : public PlatformTraceObserver {
public:
    ZirconPlatformTraceObserver();

    bool Initialize() override;

    // Can only have one observer
    void SetObserver(fit::function<void(bool)> callback) override;

private:
    async::Loop loop_;
    trace::TraceObserver observer_;
    bool enabled_;
};
#endif

} // namespace magma

#endif // ZIRCON_PLATFORM_TRACE_H
