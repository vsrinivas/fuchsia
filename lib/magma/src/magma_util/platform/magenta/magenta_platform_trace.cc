// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magenta_platform_trace.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include <stdio.h>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>

#if MAGMA_ENABLE_TRACING
#include "apps/tracing/lib/trace/event.h" // nogncheck
#include "apps/tracing/lib/trace/provider.h" // nogncheck
#include "lib/mtl/tasks/message_loop.h" // nogncheck
#include "mxio/util.h"
#endif

namespace magma {

static std::unique_ptr<MagentaPlatformTrace> g_platform_trace;

MagentaPlatformTrace::MagentaPlatformTrace()
{
#if MAGMA_ENABLE_TRACING
    trace_thread_ = std::thread([this] {
        mtl::MessageLoop loop;

        mx_status_t status;
        mx::channel trace_local, trace_remote;
        status = mx::channel::create(0, &trace_local, &trace_remote);
        if (status) {
            DLOG("Failed to create trace registry channel\n");
            return;
        }

        auto registry =
            tracing::TraceRegistryPtr::Create(fidl::InterfaceHandle<tracing::TraceRegistry>(
                std::move(trace_local), tracing::TraceRegistry::Version_));
        if (!registry) {
            DLOG("Failed to create TraceRegistry\n");
            return;
        }

        std::string service_name = "/svc/";
        service_name += tracing::TraceRegistry::Name_;

        status = mxio_service_connect(service_name.c_str(), trace_remote.release());
        if (status) {
            DLOG("Failed to connect to trace registry service\n");
            return;
        }

        InitializeTracer(std::move(registry), {"Magma Service Driver"});

        DLOG("Starting message loop");
        loop.Run();
        DLOG("Message loop returned");
    });
#endif
}


void PlatformTrace::Initialize()
{
    if (!g_platform_trace)
        g_platform_trace = std::make_unique<MagentaPlatformTrace>();
}


} // namespace magma
