// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "handler_impl.h"

#include <inttypes.h>
#include <stdio.h>

#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <trace-provider/provider.h>
#include <lib/zx/vmar.h>
#include <fbl/type_support.h>

namespace trace {
namespace internal {

TraceHandlerImpl::TraceHandlerImpl(void* buffer, size_t buffer_num_bytes,
                                   zx::fifo fifo,
                                   fbl::Vector<fbl::String> enabled_categories)
    : buffer_(buffer),
      buffer_num_bytes_(buffer_num_bytes),
      fifo_(fbl::move(fifo)),
      fifo_wait_(this, fifo_.get(),
                 ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED),
      enabled_categories_(fbl::move(enabled_categories)) {
    // Build a quick lookup table for IsCategoryEnabled().
    for (const auto& cat : enabled_categories_) {
        auto entry = fbl::make_unique<StringSetEntry>(cat.c_str());
        enabled_category_set_.insert_or_find(fbl::move(entry));
    }
}

TraceHandlerImpl::~TraceHandlerImpl() {
    zx_status_t status = zx::vmar::root_self()->unmap(
        reinterpret_cast<uintptr_t>(buffer_), buffer_num_bytes_);
    ZX_DEBUG_ASSERT(status == ZX_OK);
    status = fifo_wait_.Cancel();
    ZX_DEBUG_ASSERT(status == ZX_OK || status == ZX_ERR_NOT_FOUND);
}

zx_status_t TraceHandlerImpl::StartEngine(async_dispatcher_t* dispatcher,
                                          trace_buffering_mode_t buffering_mode,
                                          zx::vmo buffer, zx::fifo fifo,
                                          fbl::Vector<fbl::String> enabled_categories) {
    ZX_DEBUG_ASSERT(buffer);
    ZX_DEBUG_ASSERT(fifo);

    uint64_t buffer_num_bytes;
    zx_status_t status = buffer.get_size(&buffer_num_bytes);
    if (status != ZX_OK)
        return status;

    uintptr_t buffer_ptr;
    status = zx::vmar::root_self()->map(
        0u, buffer, 0u, buffer_num_bytes,
        ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &buffer_ptr);
    if (status != ZX_OK)
        return status;

    auto handler = new TraceHandlerImpl(reinterpret_cast<void*>(buffer_ptr),
                                        buffer_num_bytes, fbl::move(fifo),
                                        fbl::move(enabled_categories));

    status = handler->fifo_wait_.Begin(dispatcher);
    if (status != ZX_OK) {
        delete handler;
        return status;
    }

    status = trace_start_engine(dispatcher, handler, buffering_mode,
                                handler->buffer_, handler->buffer_num_bytes_);
    if (status != ZX_OK) {
        delete handler;
        return status;
    }

    // The handler will be destroyed in |TraceStopped()|.
    return ZX_OK;
}

zx_status_t TraceHandlerImpl::StopEngine() {
    auto status = trace_stop_engine(ZX_OK);
    if (status != ZX_OK) {
        printf("Failed to stop engine, status %s(%d)\n",
               zx_status_get_string(status), status);
    }
    return status;
}

void TraceHandlerImpl::HandleFifo(async_dispatcher_t* dispatcher,
                                  async::WaitBase* wait,
                                  zx_status_t status,
                                  const zx_packet_signal_t* signal) {
    if (status == ZX_ERR_CANCELED) {
        // The wait could be canceled if we're shutting down, e.g., the
        // program is exiting.
        return;
    }
    if (status != ZX_OK) {
        printf("TraceHandler: FIFO wait failed: status=%d\n", status);
    } else if (signal->observed & ZX_FIFO_READABLE) {
        if (ReadFifoMessage()) {
            if (wait->Begin(dispatcher) == ZX_OK) {
                return;
            }
            printf("TraceHandler: Error re-registering FIFO wait\n");
        }
    } else {
        ZX_DEBUG_ASSERT(signal->observed & ZX_FIFO_PEER_CLOSED);
    }

    // TraceManager is gone or other error with the fifo.
    StopEngine();
}

bool TraceHandlerImpl::ReadFifoMessage() {
    trace_provider_packet_t packet;
    auto status = fifo_.read(sizeof(packet), &packet, 1u, nullptr);
    ZX_DEBUG_ASSERT(status == ZX_OK);
    if (packet.reserved != 0) {
        printf("TraceHandler: Reserved field non-zero from TraceManager: %u\n",
               packet.reserved);
        return false;
    }
    switch (packet.request) {
    case TRACE_PROVIDER_BUFFER_SAVED: {
        auto wrapped_count = packet.data32;
        auto durable_data_end = packet.data64;
#if 0 // TODO(DX-367): Don't delete this, save for conversion to syslog.
        printf("TraceHandler: Received buffer_saved message"
               ", wrapped_count=%u, durable_data_end=0x%" PRIx64 "\n",
               wrapped_count, durable_data_end);
#endif
        status = MarkBufferSaved(wrapped_count, durable_data_end);
        if (status == ZX_ERR_BAD_STATE) {
            // This happens when tracing has stopped. Ignore it.
        } else if (status != ZX_OK) {
            printf("TraceHandler: MarkBufferSaved failed: status=%d\n",
                   status);
            return false;
        }
        break;
    }
    default:
        printf("TraceHandler: Bad request from TraceManager: %u\n",
               packet.request);
        return false;
    }

    return true;
}

zx_status_t TraceHandlerImpl::MarkBufferSaved(uint32_t wrapped_count,
                                              uint64_t durable_data_end) {
    return trace_engine_mark_buffer_saved(wrapped_count,
                                          durable_data_end);
}

bool TraceHandlerImpl::IsCategoryEnabled(const char* category) {
    if (enabled_categories_.size() == 0) {
      // If none are specified, enable all categories.
      return true;
    }
    return enabled_category_set_.find(category) != enabled_category_set_.end();
}

void TraceHandlerImpl::TraceStarted() {
    trace_provider_packet_t packet{};
    packet.request = TRACE_PROVIDER_STARTED;
    packet.data32 = TRACE_PROVIDER_FIFO_PROTOCOL_VERSION;
    auto status = fifo_.write(sizeof(packet), &packet, 1, nullptr);
    ZX_DEBUG_ASSERT(status == ZX_OK ||
                    status == ZX_ERR_PEER_CLOSED);
}

void TraceHandlerImpl::TraceStopped(async_dispatcher_t* dispatcher, zx_status_t disposition,
                                    size_t buffer_bytes_written) {
    // There's no need to notify the trace manager that records were dropped
    // here. That can be determined from the buffer header.
    delete this;
}

void TraceHandlerImpl::NotifyBufferFull(uint32_t wrapped_count,
                                        uint64_t durable_data_end) {
    trace_provider_packet_t packet{};
    packet.request = TRACE_PROVIDER_SAVE_BUFFER;
    packet.data32 = wrapped_count;
    packet.data64 = durable_data_end;
    auto status = fifo_.write(sizeof(packet), &packet, 1, nullptr);
    // There's something wrong in our protocol or implementation if we fill
    // the fifo buffer.
    ZX_DEBUG_ASSERT(status == ZX_OK ||
                    status == ZX_ERR_PEER_CLOSED);
}

} // namespace internal
} // namespace trace
