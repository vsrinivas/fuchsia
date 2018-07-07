// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "provider_impl.h"

#include <fbl/algorithm.h>
#include <fbl/type_support.h>
#include <lib/fdio/util.h>
#include <lib/fidl/coding.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include "handler_impl.h"
#include "trace_provider.fidl.h"

namespace trace {
namespace internal {

TraceProviderImpl::TraceProviderImpl(async_dispatcher_t* dispatcher, zx::channel channel)
    : dispatcher_(dispatcher), connection_(this, fbl::move(channel)) {
}

TraceProviderImpl::~TraceProviderImpl() = default;

void TraceProviderImpl::Start(zx::vmo buffer, zx::fifo fifo,
                              fbl::Vector<fbl::String> enabled_categories) {
    if (running_)
        return;

    zx_status_t status = TraceHandlerImpl::StartEngine(
        dispatcher_, fbl::move(buffer), fbl::move(fifo),
        fbl::move(enabled_categories));
    if (status == ZX_OK)
        running_ = true;
}

void TraceProviderImpl::Stop() {
    if (!running_)
        return;

    running_ = false;
    TraceHandlerImpl::StopEngine();
}

TraceProviderImpl::Connection::Connection(TraceProviderImpl* impl,
                                          zx::channel channel)
    : impl_(impl), channel_(fbl::move(channel)),
      wait_(this, channel_.get(),
            ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED) {
    zx_status_t status = wait_.Begin(impl_->dispatcher_);
    if (status != ZX_OK) {
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
        printf("TraceProvider wait failed: status=%d\n", status);
    } else if (signal->observed & ZX_CHANNEL_READABLE) {
        if (ReadMessage()) {
            if (wait_.Begin(dispatcher) == ZX_OK) {
                return;
            }
        } else {
            printf("TraceProvider received invalid FIDL message or failed to send reply.\n");
        }
    } else {
        ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
    }

    Close();
}

bool TraceProviderImpl::Connection::ReadMessage() {
    FIDL_ALIGNDECL uint8_t buffer[16 * 1024];
    uint32_t num_bytes = 0u;
    zx_handle_t handles[2];
    uint32_t num_handles = 0u;
    zx_status_t status = channel_.read(
        0u, buffer, sizeof(buffer), &num_bytes,
        handles, static_cast<uint32_t>(fbl::count_of(handles)), &num_handles);
    if (status != ZX_OK) {
        printf("TraceProvider channel read failed\n");
        return false;
    }

    if (!DecodeAndDispatch(buffer, num_bytes, handles, num_handles)) {
        printf("TraceProvider DecodeAndDispatch failed\n");
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
        auto buffer = zx::vmo(request->buffer);
        auto fifo = zx::fifo(request->fifo);
        fbl::Vector<fbl::String> categories;
        auto strings = reinterpret_cast<fidl_string_t*>(request->categories.data);
        for (size_t i = 0; i < request->categories.count; i++) {
            categories.push_back(fbl::String(strings[i].data, strings[i].size));
        }
        impl_->Start(fbl::move(buffer), fbl::move(fifo), fbl::move(categories));
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
        impl_->Stop();
    }
}

} // namespace internal
} // namespace trace

trace_provider_t* trace_provider_create(async_dispatcher_t* dispatcher) {
    ZX_DEBUG_ASSERT(dispatcher);

    // Connect to the trace registry.
    zx::channel registry_client;
    zx::channel registry_service;
    zx_status_t status = zx::channel::create(0u, &registry_client, &registry_service);
    if (status != ZX_OK)
        return nullptr;

    status = fdio_service_connect("/svc/fuchsia.tracelink.Registry",
                                  registry_service.release()); // takes ownership
    if (status != ZX_OK)
        return nullptr;

    // Create the channel to which we will bind the trace provider.
    zx::channel provider_client;
    zx::channel provider_service;
    status = zx::channel::create(0u, &provider_client, &provider_service);
    if (status != ZX_OK)
        return nullptr;

    // Register the trace provider.
    fuchsia_tracelink_RegistryRegisterTraceProviderRequest request = {};
    request.hdr.ordinal = fuchsia_tracelink_RegistryRegisterTraceProviderOrdinal;
    request.provider = FIDL_HANDLE_PRESENT;
    zx_handle_t handles[] = {provider_client.release()};
    status = registry_client.write(0u, &request, sizeof(request),
                                   handles, static_cast<uint32_t>(fbl::count_of(handles)));
    if (status != ZX_OK)
        return nullptr;

    return new trace::internal::TraceProviderImpl(dispatcher, fbl::move(provider_service));
}

void trace_provider_destroy(trace_provider_t* provider) {
    ZX_DEBUG_ASSERT(provider);
    delete static_cast<trace::internal::TraceProviderImpl*>(provider);
}
