// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(PT-63): Remove the "_etc" suffix on all API functions.

#include "provider_impl.h"

#include <stdio.h>

#include <fuchsia/tracelink/c/fidl.h>
#include <lib/async/default.h>
#include <lib/fidl/coding.h>
#include <lib/zx/process.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <utility>

#include "session.h"
#include "utils.h"

namespace trace {
namespace internal {

TraceProviderImpl::TraceProviderImpl(async_dispatcher_t* dispatcher,
                                     zx::channel channel)
    : dispatcher_(dispatcher),
      connection_(this, std::move(channel)) {
}

TraceProviderImpl::~TraceProviderImpl() = default;

void TraceProviderImpl::Start(trace_buffering_mode_t buffering_mode,
                              zx::vmo buffer, zx::fifo fifo,
                              std::vector<std::string> enabled_categories) {
    Session::StartEngine(
        dispatcher_, buffering_mode, std::move(buffer), std::move(fifo),
        std::move(enabled_categories));
}

void TraceProviderImpl::Stop() {
    Session::StopEngine();
}

void TraceProviderImpl::OnClose() {
    Stop();
}

TraceProviderImpl::Connection::Connection(TraceProviderImpl* impl,
                                          zx::channel channel)
    : impl_(impl), channel_(std::move(channel)),
      wait_(this, channel_.get(),
            ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED) {
    zx_status_t status = wait_.Begin(impl_->dispatcher_);
    if (status != ZX_OK) {
        fprintf(stderr, "TraceProvider: begin wait failed: status=%d(%s)\n",
                status, zx_status_get_string(status));
        Close();
    }
}

TraceProviderImpl::Connection::~Connection() {
    Close();
}

void TraceProviderImpl::Connection::Handle(
    async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
    const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
        fprintf(stderr, "TraceProvider: wait failed: status=%d(%s)\n",
                status, zx_status_get_string(status));
    } else if (signal->observed & ZX_CHANNEL_READABLE) {
        if (ReadMessage()) {
            if (wait_.Begin(dispatcher) == ZX_OK) {
                return;
            }
        } else {
            fprintf(stderr,
                    "TraceProvider: received invalid FIDL message or failed to send reply\n");
        }
    } else {
        ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
    }

    Close();
}

bool TraceProviderImpl::Connection::ReadMessage() {
    FIDL_ALIGNDECL uint8_t buffer[16 * 1024];
    uint32_t num_bytes = 0u;
    constexpr uint32_t kNumHandles = 2;
    zx_handle_t handles[kNumHandles];
    uint32_t num_handles = 0u;
    zx_status_t status = channel_.read(
        0u, buffer, handles, sizeof(buffer), kNumHandles,
        &num_bytes, &num_handles);
    if (status != ZX_OK) {
        fprintf(stderr, "TraceProvider: channel read failed: status=%d(%s)\n",
                status, zx_status_get_string(status));
        return false;
    }

    if (!DecodeAndDispatch(buffer, num_bytes, handles, num_handles)) {
        fprintf(stderr, "TraceProvider: DecodeAndDispatch failed\n");
        zx_handle_close_many(handles, num_handles);
        return false;
    }

    return true;
}

bool TraceProviderImpl::Connection::DecodeAndDispatch(
    uint8_t* buffer, uint32_t num_bytes,
    zx_handle_t* handles, uint32_t num_handles) {
    if (num_bytes < sizeof(fidl_message_header_t)) {
        zx_handle_close_many(handles, num_handles);
        return false;
    }

    auto hdr = reinterpret_cast<fidl_message_header_t*>(buffer);
    switch (hdr->ordinal) {
    case fuchsia_tracelink_ProviderStartOrdinal: {
        zx_status_t status = fidl_decode(&fuchsia_tracelink_ProviderStartRequestTable,
                                         buffer, num_bytes, handles, num_handles,
                                         nullptr);
        if (status != ZX_OK)
            break;

        auto request = reinterpret_cast<fuchsia_tracelink_ProviderStartRequest*>(buffer);
        auto buffering_mode = request->buffering_mode;
        auto buffer = zx::vmo(request->buffer);
        auto fifo = zx::fifo(request->fifo);
        std::vector<std::string> categories;
        auto strings = reinterpret_cast<fidl_string_t*>(request->categories.data);
        for (size_t i = 0; i < request->categories.count; i++) {
            categories.push_back(std::string(strings[i].data, strings[i].size));
        }
        trace_buffering_mode_t trace_buffering_mode;
        switch (buffering_mode) {
        case fuchsia_tracelink_BufferingMode_ONESHOT:
            trace_buffering_mode = TRACE_BUFFERING_MODE_ONESHOT;
            break;
        case fuchsia_tracelink_BufferingMode_CIRCULAR:
            trace_buffering_mode = TRACE_BUFFERING_MODE_CIRCULAR;
            break;
        case fuchsia_tracelink_BufferingMode_STREAMING:
            trace_buffering_mode = TRACE_BUFFERING_MODE_STREAMING;
            break;
        default:
            return false;
        }
        impl_->Start(trace_buffering_mode, std::move(buffer), std::move(fifo),
                     std::move(categories));
        return true;
    }
    case fuchsia_tracelink_ProviderStopOrdinal: {
        zx_status_t status = fidl_decode(&fuchsia_tracelink_ProviderStopRequestTable,
                                         buffer, num_bytes, handles, num_handles,
                                         nullptr);
        if (status != ZX_OK)
            break;

        impl_->Stop();
        return true;
    }
    }
    return false;
}

void TraceProviderImpl::Connection::Close() {
    if (channel_) {
        wait_.Cancel();
        channel_.reset();
        impl_->OnClose();
    }
}

} // namespace internal
} // namespace trace

trace_provider_t* trace_provider_create_with_name_etc(
        zx_handle_t to_service_h, async_dispatcher_t* dispatcher,
        const char* name) {
    zx::channel to_service(to_service_h);

    ZX_DEBUG_ASSERT(to_service.is_valid());
    ZX_DEBUG_ASSERT(dispatcher);

    // Create the channel to which we will bind the trace provider.
    zx::channel provider_client;
    zx::channel provider_service;
    zx_status_t status =
        zx::channel::create(0u, &provider_client, &provider_service);
    if (status != ZX_OK) {
        fprintf(stderr, "TraceProvider: channel create failed: status=%d(%s)\n",
                status, zx_status_get_string(status));
        return nullptr;
    }

    // Register the trace provider.
    status = fuchsia_tracelink_RegistryRegisterTraceProvider(
        to_service.get(), provider_client.release(),
        trace::internal::GetPid(), name, strlen(name));
    if (status != ZX_OK) {
        fprintf(stderr, "TraceProvider: registry failed: status=%d(%s)\n",
                status, zx_status_get_string(status));
        return nullptr;
    }
    // Note: |to_service| can be closed now. Let it close as a consequence
    // of going out of scope.

    return new trace::internal::TraceProviderImpl(
        dispatcher, std::move(provider_service));
}

trace_provider_t* trace_provider_create_etc(zx_handle_t to_service,
                                            async_dispatcher_t* dispatcher) {
    auto self = zx::process::self();
    char name[ZX_MAX_NAME_LEN];
    auto status = self->get_property(ZX_PROP_NAME, name, sizeof(name));
    if (status != ZX_OK) {
        fprintf(stderr, "TraceProvider: error getting process name: status=%d(%s)\n",
                status, zx_status_get_string(status));
        name[0] = '\0';
    }
    return trace_provider_create_with_name_etc(to_service, dispatcher, name);
}

trace_provider_t* trace_provider_create_synchronously_etc(
        zx_handle_t to_service_h, async_dispatcher_t* dispatcher,
        const char* name, bool* out_manager_is_tracing_already) {
    zx::channel to_service(to_service_h);

    ZX_DEBUG_ASSERT(to_service.is_valid());
    ZX_DEBUG_ASSERT(dispatcher);

    // Create the channel to which we will bind the trace provider.
    zx::channel provider_client;
    zx::channel provider_service;
    zx_status_t status =
        zx::channel::create(0u, &provider_client, &provider_service);
    if (status != ZX_OK) {
        fprintf(stderr, "TraceProvider: channel create failed: status=%d(%s)\n",
                status, zx_status_get_string(status));
        return nullptr;
    }

    // Register the trace provider.
    zx_status_t registry_status;
    bool manager_is_tracing_already;
    status = fuchsia_tracelink_RegistryRegisterTraceProviderSynchronously(
        to_service.get(), provider_client.release(),
        trace::internal::GetPid(), name, strlen(name),
        &registry_status, &manager_is_tracing_already);
    if (status != ZX_OK) {
        fprintf(stderr, "TraceProvider: RegisterTraceProviderSynchronously failed: status=%d(%s)\n",
                status, zx_status_get_string(status));
        return nullptr;
    }
    if (registry_status != ZX_OK) {
        fprintf(stderr, "TraceProvider: registry failed: status=%d(%s)\n",
                status, zx_status_get_string(status));
        return nullptr;
    }
    // Note: |to_service| can be closed now. Let it close as a consequence
    // of going out of scope.

    if (out_manager_is_tracing_already)
        *out_manager_is_tracing_already = manager_is_tracing_already;
    return new trace::internal::TraceProviderImpl(dispatcher,
                                                  std::move(provider_service));
}

void trace_provider_destroy(trace_provider_t* provider) {
    ZX_DEBUG_ASSERT(provider);
    delete static_cast<trace::internal::TraceProviderImpl*>(provider);
}
