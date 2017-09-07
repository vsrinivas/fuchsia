// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "provider_impl.h"

#include <magenta/assert.h>
#include <magenta/syscalls.h>

#include <mxio/util.h>
#include <fbl/algorithm.h>
#include <fbl/type_support.h>

#include "handler_impl.h"

namespace {

// Handrolled FIDL structures...

struct header_v0 {
    uint32_t size;
    uint32_t version;
    uint32_t ordinal;
    uint32_t flags;
};

struct header_v1 : header_v0 {
    uint64_t id;
};

struct message {
    uint32_t size;
    uint32_t version;
};

// TraceProvider::Start(handle<vmo> buffer, handle<eventpair> fence, array<string> categories)
//     => (bool success)
struct start : message {
    uint32_t buffer; // handle<vmo>
    uint32_t fence; // handle<eventpair>
    // categories here... (not parsed)
};
struct start_reply : message {
    uint8_t success; // bool, least significant bit
    uint8_t padding[7];
};

// TraceRegistry::RegisterTraceProvider(TraceProvider provider, string? label)
struct register_trace_provider : message {
    uint32_t provider; // TraceProvider
    uint32_t padding;
    uint64_t label; // string?
};

} // namespace

namespace trace {
namespace internal {

TraceProviderImpl::TraceProviderImpl(async_t* async, mx::channel channel)
    : async_(async), connection_(this, fbl::move(channel)) {
}

TraceProviderImpl::~TraceProviderImpl() = default;

bool TraceProviderImpl::Start(mx::vmo buffer, mx::eventpair fence) {
    if (running_)
        return false;

    mx_status_t status = TraceHandlerImpl::StartEngine(
        async_, fbl::move(buffer), fbl::move(fence));
    if (status != MX_OK)
        return false;

    running_ = true;
    return true;
}

void TraceProviderImpl::Stop() {
    if (!running_)
        return;

    running_ = false;
    TraceHandlerImpl::StopEngine();
}

TraceProviderImpl::Connection::Connection(TraceProviderImpl* impl,
                                          mx::channel channel)
    : impl_(impl), channel_(fbl::move(channel)),
      wait_(channel_.get(),
            MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED) {
    wait_.set_handler(fbl::BindMember(this, &Connection::Handle));

    mx_status_t status = wait_.Begin(impl_->async_);
    MX_DEBUG_ASSERT(status == MX_OK || status == MX_ERR_BAD_STATE);
}

TraceProviderImpl::Connection::~Connection() {
    Close();
}

async_wait_result_t TraceProviderImpl::Connection::Handle(
    async_t* async, mx_status_t status, const mx_packet_signal_t* signal) {
    if (status != MX_OK) {
        printf("TraceProvider wait failed: status=%d\n", status);
        return ASYNC_WAIT_FINISHED;
    }

    if (signal->observed & MX_CHANNEL_READABLE) {
        if (ReadMessage())
            return ASYNC_WAIT_AGAIN;
        printf("TraceProvider received invalid FIDL message or failed to send reply.\n");
    } else {
        MX_DEBUG_ASSERT(signal->observed & MX_CHANNEL_PEER_CLOSED);
    }

    Close();
    return ASYNC_WAIT_FINISHED;
}

bool TraceProviderImpl::Connection::ReadMessage() {
    // Using handrolled FIDL.

    uint8_t buffer[16 * 1024];
    mx_handle_t unowned_handles[2];
    uint32_t num_bytes = 0u;
    uint32_t num_handles = 0u;
    mx_status_t status = channel_.read(
        0u, buffer, sizeof(buffer), &num_bytes,
        unowned_handles, fbl::count_of(unowned_handles), &num_handles);
    if (status != MX_OK)
        return false;

    mx::handle handles[2];
    for (size_t i = 0; i < num_handles; i++) {
        handles[i].reset(unowned_handles[i]); // take ownership
    }

    if (num_bytes < sizeof(header_v0))
        return false;

    const header_v0* h0 = reinterpret_cast<header_v0*>(buffer);
    if (h0->size & 7)
        return false;
    if (h0->size < sizeof(header_v0) || h0->size > num_bytes)
        return false;

    const header_v1* h1 = nullptr;
    if (h0->version >= 1) {
        if (h0->size < sizeof(header_v1))
            return false;
        h1 = static_cast<const header_v1*>(h0);
    }

    num_bytes -= h0->size;
    if (num_bytes < sizeof(message))
        return false;

    const message* m = reinterpret_cast<message*>(buffer + h0->size);
    if (m->size & 7)
        return false;
    if (m->size < sizeof(message) || m->size > num_bytes)
        return false;

    switch (h0->ordinal) {
    case 0: {
        // TraceProvider::Start(handle<vmo> buffer, handle<eventpair> fence,
        //     array<string> categories) => (bool success)
        if (!h1) // request id only present in v1 and beyond
            return false;
        if (!(h1->flags & 1)) // expects response
            return false;
        if (num_bytes < sizeof(start))
            return false;
        const start* s = static_cast<const start*>(m);
        if (s->buffer != 0u || s->fence != 1u)
            return false;

        bool success = impl_->Start(
            mx::vmo(fbl::move(handles[s->buffer])),
            mx::eventpair(fbl::move(handles[s->fence])));

        // Send reply.
        struct {
            header_v1 h;
            start_reply m;
        } reply = {};
        reply.h.size = 24;
        reply.h.version = 1;
        reply.h.ordinal = 0;
        reply.h.flags = 2;
        reply.h.id = h1->id;
        reply.m.size = 16;
        reply.m.version = 0;
        reply.m.success = success ? 1 : 0;
        status = channel_.write(0u, &reply, sizeof(reply), nullptr, 0u);
        if (status != MX_OK)
            return false;
        return true;
    }
    case 1:
        // TraceProvider::Stop()
        impl_->Stop();
        return true;
    case 2:
        // TraceProvider::Dump(handle<socket> output)
        return true; // ignored
    default:
        return false;
    }
}

void TraceProviderImpl::Connection::Close() {
    if (channel_) {
        wait_.Cancel(impl_->async_);
        channel_.reset();
        impl_->Stop();
    }
}

} // namespace internal
} // namespace trace

trace_provider_t* trace_provider_create(async_t* async) {
    MX_DEBUG_ASSERT(async);

    // Connect to the trace registry.
    mx::channel registry_client;
    mx::channel registry_service;
    mx_status_t status = mx::channel::create(0u, &registry_client, &registry_service);
    if (status != MX_OK)
        return nullptr;

    status = mxio_service_connect("/svc/tracing::TraceRegistry",
                                  registry_service.release()); // takes ownership
    if (status != MX_OK)
        return nullptr;

    // Create the channel to which we will bind the trace provider.
    mx::channel provider_client;
    mx::channel provider_service;
    status = mx::channel::create(0u, &provider_client, &provider_service);
    if (status != MX_OK)
        return nullptr;

    // Invoke TraceRegistry::RegisterTraceProvider(TraceProvider provider, string? label)
    // TODO(MG-1036): We currently set the label to null.  Once tracing fully migrates
    // to Magenta and we publish the provider via the hub we will no longer need to
    // specify a label at all since we will be able to identify providers based on their
    // path within the hub's directory structure.
    struct {
        header_v0 h;
        register_trace_provider m;
    } call = {};
    call.h.size = 16;
    call.h.version = 0;
    call.h.ordinal = 0;
    call.h.flags = 0;
    call.m.size = 24;
    call.m.version = 0;
    call.m.provider = 0;
    call.m.label = 0;
    mx_handle_t handles[] = {provider_client.release()};
    status = registry_client.write(0u, &call, sizeof(call),
                                   handles, fbl::count_of(handles));
    if (status != MX_OK) {
        provider_client.reset(handles[0]); // take back ownership after failure
        return nullptr;
    }

    return new trace::internal::TraceProviderImpl(async, fbl::move(provider_service));
}

void trace_provider_destroy(trace_provider_t* provider) {
    MX_DEBUG_ASSERT(provider);
    delete static_cast<trace::internal::TraceProviderImpl*>(provider);
}
