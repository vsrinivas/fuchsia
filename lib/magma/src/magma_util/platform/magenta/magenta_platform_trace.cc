// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magenta_platform_trace.h"

#include <memory>

#include "magma_util/dlog.h"
#include "magma_util/macros.h"

namespace magma {

#if MAGMA_ENABLE_TRACING

static std::unique_ptr<MagentaPlatformTrace> g_platform_trace;

MagentaPlatformTrace::MagentaPlatformTrace() : trace_provider_(loop_.async())
{
    mx_status_t status = loop_.StartThread();
    if (status != MX_OK) {
        DLOG("Failed to start async loop, magma platform tracing will not work");
    }
}

void PlatformTrace::Initialize()
{
    if (!g_platform_trace)
        g_platform_trace = std::make_unique<MagentaPlatformTrace>();
}

#else

void PlatformTrace::Initialize() {}

#endif


} // namespace magma
