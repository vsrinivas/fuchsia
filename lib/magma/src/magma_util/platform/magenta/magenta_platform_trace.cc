// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magenta_platform_trace.h"
#include "magenta_platform_launcher.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include <magenta/process.h>
#include <magenta/processargs.h>
#include <stdio.h>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>

#if MAGMA_ENABLE_TRACING
#include "apps/tracing/lib/trace/event.h" // nogncheck
#include "apps/tracing/lib/trace/provider.h" // nogncheck
#include "apps/tracing/services/trace_controller.fidl.h" // nogncheck
#include "lib/mtl/tasks/message_loop.h" // nogncheck
#include "lib/mtl/tasks/message_loop_handler.h" // nogncheck
#endif

namespace magma {

static std::unique_ptr<MagentaPlatformTrace> g_platform_trace;

class TraceManager {
public:
    bool Start()
    {
        struct stat s;
        if (stat(kTraceManager, &s) != 0)
            return DRETF(false, "Couldn't find trace_manager");

        mx::channel::create(0, &environment_, &environment_client_);
        mx::channel::create(0, &services_, &services_client_);

        const uint32_t kHandleCount = 2;
        mx_handle_t init_hnds[] = {environment_.get(), services_.get()};
        uint32_t init_ids[] = {
            MX_HND_INFO(MX_HND_TYPE_APPLICATION_ENVIRONMENT, 0),
            MX_HND_INFO(MX_HND_TYPE_APPLICATION_SERVICES, 0),
        };
        const char* argv[] = {kTraceManager};
        MagentaPlatformLauncher::Launch(mx_job_default(), "trace_manager", countof(argv), argv,
                                        NULL, init_hnds, init_ids, kHandleCount);
        return true;
    }

    bool ConnectToService(const uint8_t* bytes, uint32_t bytes_count, const mx_handle_t* handles,
                          uint32_t handle_count)
    {
        mx_status_t status = services_client_.write(0, bytes, bytes_count, handles, handle_count);
        if (status != NO_ERROR)
            return DRETF(false, "channel write failed: %d", status);
        return true;
    }

private:
    const char* kTraceManager = "/system/apps/trace_manager";

    mx::channel environment_, environment_client_;
    mx::channel services_, services_client_;
};

MagentaPlatformTrace::MagentaPlatformTrace()
{
    trace_manager_ = std::make_unique<TraceManager>();

    if (!trace_manager_->Start()) {
        DLOG("failed to start trace manager");
        return;
    }

#if MAGMA_ENABLE_TRACING
    trace_thread_ = std::thread([this] {
        mtl::MessageLoop loop;

        mx::channel services_local, services_remote;
        mx::channel::create(0, &services_local, &services_remote);

        auto service_provider =
            app::ServiceProviderPtr::Create(fidl::InterfaceHandle<app::ServiceProvider>(
                std::move(services_local), app::ServiceProvider::Version_));
        if (!service_provider) {
            DLOG("failed to create ServiceProviderPtr");
            return;
        }

        ConnectToService(std::move(services_remote));

        mx::channel trace_local, trace_remote;
        mx::channel::create(0, &trace_local, &trace_remote);

        auto registry =
            tracing::TraceRegistryPtr::Create(fidl::InterfaceHandle<tracing::TraceRegistry>(
                std::move(trace_local), tracing::TraceRegistry::Version_));
        if (!registry) {
            DLOG("Failed to create TraceRegistry\n");
            return;
        }

        service_provider->ConnectToService(tracing::TraceRegistry::Name_, std::move(trace_remote));

        tracing::TraceSettings settings = {"Magma Service Driver"};
        InitializeTracer(std::move(registry), settings);

        DLOG("Starting message loop");
        loop.Run();
        DLOG("Message loop returned");
    });
#endif
}

void MagentaPlatformTrace::ConnectToService(mx::channel app_channel)
{
    auto reader_thread = std::thread([ this, app_channel = std::move(app_channel) ] {
        std::vector<uint8_t> bytes(4096);
        std::vector<mx_handle_t> handles(4);

        while (true) {
            mx_signals_t signals = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
            mx_signals_t pending = 0;

            mx_status_t status = app_channel.wait_one(signals, MX_TIME_INFINITE, &pending);
            if (status != NO_ERROR) {
                DLOG("wait_one returned status %d", status);
                continue;
            }

            if (pending & MX_CHANNEL_READABLE) {
                uint32_t bytes_read, handles_read;
                status = app_channel.read(0, bytes.data(), bytes.size(), &bytes_read,
                                          handles.data(), handles.size(), &handles_read);
                if (status != NO_ERROR) {
                    DLOG("channel read returned status %d", status);
                    continue;
                }
                DLOG("forwarding %u bytes %u handles bytes.size() %zu", bytes_read, handles_read,
                     bytes.size());
                trace_manager_->ConnectToService(bytes.data(), bytes_read, handles.data(),
                                                 handles_read);
            }

            if (pending & MX_CHANNEL_PEER_CLOSED) {
                DLOG("got peer closed");
                break;
            }
        }
    });
    reader_thread.detach();
}

void PlatformTrace::Initialize()
{
    if (!g_platform_trace)
        g_platform_trace = std::make_unique<MagentaPlatformTrace>();
}

PlatformTrace* PlatformTrace::GetInstance()
{
    Initialize();
    return g_platform_trace.get();
}

} // namespace magma
