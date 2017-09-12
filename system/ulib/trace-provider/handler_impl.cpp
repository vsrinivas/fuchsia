// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "handler_impl.h"

#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <zx/vmar.h>
#include <fbl/type_support.h>

namespace trace {
namespace internal {

TraceHandlerImpl::TraceHandlerImpl(void* buffer, size_t buffer_num_bytes,
                                   zx::eventpair fence)
    : buffer_(buffer), buffer_num_bytes_(buffer_num_bytes), fence_(fbl::move(fence)) {}

TraceHandlerImpl::~TraceHandlerImpl() {
    zx_status_t status = zx::vmar::root_self().unmap(
        reinterpret_cast<uintptr_t>(buffer_), buffer_num_bytes_);
    ZX_DEBUG_ASSERT(status == ZX_OK);
}

zx_status_t TraceHandlerImpl::StartEngine(async_t* async,
                                          zx::vmo buffer, zx::eventpair fence) {
    ZX_DEBUG_ASSERT(buffer);
    ZX_DEBUG_ASSERT(fence);

    uint64_t buffer_num_bytes;
    zx_status_t status = buffer.get_size(&buffer_num_bytes);
    if (status != ZX_OK)
        return status;

    uintptr_t buffer_ptr;
    status = zx::vmar::root_self().map(
        0u, buffer, 0u, buffer_num_bytes,
        ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &buffer_ptr);
    if (status != ZX_OK)
        return status;

    auto handler = new TraceHandlerImpl(reinterpret_cast<void*>(buffer_ptr),
                                        buffer_num_bytes, fbl::move(fence));
    status = trace_start_engine(async, handler,
                                handler->buffer_, handler->buffer_num_bytes_);
    if (status != ZX_OK) {
        delete handler;
        return status;
    }

    // The handler will be destroyed in |TraceStopped()|.
    return ZX_OK;
}

zx_status_t TraceHandlerImpl::StopEngine() {
    return trace_stop_engine(ZX_OK);
}

bool TraceHandlerImpl::IsCategoryEnabled(const char* category) {
    // TODO: Implement category filters.
    return true;
}

void TraceHandlerImpl::TraceStopped(async_t* async, zx_status_t disposition,
                                    size_t buffer_bytes_written) {
    // TODO: Report the disposition and bytes written back to the tracing system
    // so it has a better idea of what happened.
    delete this;
}

} // namespace internal
} // namespace trace
