// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "provider_impl.h"

#include <inttypes.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <fdio/util.h>
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

struct message {
    uint32_t size;
    uint32_t version;
};

// TraceProvider::Start(handle<vmo> buffer, handle<eventpair> fence, array<string> categories)
struct start : message {
    uint32_t buffer; // handle<vmo>
    uint32_t fence; // handle<eventpair>
    uint64_t category_array_header_length; // always 8
    uint32_t category_offsets_array_length; // in bytes
    uint32_t num_categories;
    uint64_t category_offsets[]; // offset to string from this location
};

constexpr unsigned kExpectedCategoryArrayHeaderLength = 8;

struct string_entry {
    uint32_t entry_length; // not including any padding
    uint32_t string_length; // Note: there is no trailing NUL
    char text[];
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

TraceProviderImpl::TraceProviderImpl(async_t* async, zx::channel channel)
    : async_(async), connection_(this, fbl::move(channel)) {
}

TraceProviderImpl::~TraceProviderImpl() = default;

void TraceProviderImpl::Start(zx::vmo buffer, zx::eventpair fence,
                              fbl::Vector<fbl::String> enabled_categories) {
    if (running_)
        return;

    zx_status_t status = TraceHandlerImpl::StartEngine(
        async_, fbl::move(buffer), fbl::move(fence),
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
    zx_status_t status = wait_.Begin(impl_->async_);
    ZX_DEBUG_ASSERT(status == ZX_OK || status == ZX_ERR_BAD_STATE);
}

TraceProviderImpl::Connection::~Connection() {
    Close();
}

async_wait_result_t TraceProviderImpl::Connection::Handle(
    async_t* async, zx_status_t status, const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
        printf("TraceProvider wait failed: status=%d\n", status);
        return ASYNC_WAIT_FINISHED;
    }

    if (signal->observed & ZX_CHANNEL_READABLE) {
        if (ReadMessage())
            return ASYNC_WAIT_AGAIN;
        printf("TraceProvider received invalid FIDL message or failed to send reply.\n");
    } else {
        ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
    }

    Close();
    return ASYNC_WAIT_FINISHED;
}

bool TraceProviderImpl::Connection::ReadMessage() {
    // Using handrolled FIDL.

    uint8_t buffer[16 * 1024];
    zx_handle_t unowned_handles[2];
    uint32_t num_bytes = 0u;
    uint32_t num_handles = 0u;
    zx_status_t status = channel_.read(
        0u, buffer, sizeof(buffer), &num_bytes,
        unowned_handles, static_cast<uint32_t>(fbl::count_of(unowned_handles)), &num_handles);
    if (status != ZX_OK)
        return false;

    zx::handle handles[2];
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
        //     array<string> categories)
        // Note: There is no response so may be a version 0 packet.
        if (num_bytes < sizeof(start))
            return false;
        const start* s = static_cast<const start*>(m);
        if (s->buffer != 0u || s->fence != 1u)
            return false;

        fbl::Vector<fbl::String> enabled_categories;
        if (s->category_array_header_length != kExpectedCategoryArrayHeaderLength) {
            printf("%s: unexpected value for category_array_header_length field of fidl start message: %" PRIu64 "\n",
                   __func__, s->category_array_header_length);
            return false;
        }
        if (s->category_offsets_array_length - 2 * sizeof(uint32_t) != s->num_categories * sizeof(uint64_t)) {
            printf("%s: category offsets array error: length %u for %u categories\n",
                   __func__, s->category_offsets_array_length, s->num_categories);
            return false;
        }
        auto message_end = reinterpret_cast<const char*>(s) + num_bytes;
        for (uint32_t i = 0; i < s->num_categories; ++i) {
            auto str_ptr = reinterpret_cast<const char*>(&s->category_offsets[i]) + s->category_offsets[i];
            if (s->category_offsets[i] >= num_bytes ||
                str_ptr + 2 * sizeof(uint32_t) >= message_end) {
                printf("%s: category offset error, too large for message: %" PRIu64 "\n",
                       __func__, s->category_offsets[i]);
                return false;
            }
            auto str = reinterpret_cast<const string_entry*>(str_ptr);
            auto str_end = str_ptr + str->entry_length;
            if (str_end > message_end ||
                str->string_length >= str->entry_length) {
                printf("%s: string length error: entry_length %u, string_length %u\n",
                       __func__, str->entry_length, str->string_length);
                return false;
            }
            enabled_categories.push_back(fbl::String(str->text, str->string_length));
        }

        impl_->Start(
            zx::vmo(fbl::move(handles[s->buffer])),
            zx::eventpair(fbl::move(handles[s->fence])),
            fbl::move(enabled_categories));

        // The provider hasn't necessarily started yet. We've just asked it
        // to. It will tell the trace manager directly via |fence| when it
        // is ready. If Start() failed then |fence| will have been closed.
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
    ZX_DEBUG_ASSERT(async);

    // Connect to the trace registry.
    zx::channel registry_client;
    zx::channel registry_service;
    zx_status_t status = zx::channel::create(0u, &registry_client, &registry_service);
    if (status != ZX_OK)
        return nullptr;

    status = fdio_service_connect("/svc/tracing::TraceRegistry",
                                  registry_service.release()); // takes ownership
    if (status != ZX_OK)
        return nullptr;

    // Create the channel to which we will bind the trace provider.
    zx::channel provider_client;
    zx::channel provider_service;
    status = zx::channel::create(0u, &provider_client, &provider_service);
    if (status != ZX_OK)
        return nullptr;

    // Invoke TraceRegistry::RegisterTraceProvider(TraceProvider provider, string? label)
    // TODO(ZX-1036): We currently set the label to null.  Once tracing fully migrates
    // to Zircon and we publish the provider via the hub we will no longer need to
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
    zx_handle_t handles[] = {provider_client.release()};
    status = registry_client.write(0u, &call, sizeof(call),
                                   handles, static_cast<uint32_t>(fbl::count_of(handles)));
    if (status != ZX_OK) {
        provider_client.reset(handles[0]); // take back ownership after failure
        return nullptr;
    }

    return new trace::internal::TraceProviderImpl(async, fbl::move(provider_service));
}

void trace_provider_destroy(trace_provider_t* provider) {
    ZX_DEBUG_ASSERT(provider);
    delete static_cast<trace::internal::TraceProviderImpl*>(provider);
}
