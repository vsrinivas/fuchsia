// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Trace handlers manage the configuration, lifecycle, and external communication
// of the trace engine.
//
// See <trace-engine/handler.h> for the C API and more detailed documentation.
//

#pragma once

#include <trace-engine/handler.h>

#ifdef __cplusplus

namespace trace {

// Implements |trace_handler_t|.
// Make sure the trace has fully stopped before destroying the handler object.
class TraceHandler : public trace_handler_t {
public:
    TraceHandler();
    virtual ~TraceHandler();

    // Called by the trace engine to ask whether the specified category is enabled.
    //
    // This method may be called frequently so it must be efficiently implemented.
    // Clients may cache the results while a trace is running; dynamic changes
    // to the enabled categories may go unnoticed until the next trace.
    //
    // |category| is the name of the category.
    //
    // Called by instrumentation on any thread.  Must be thread-safe.
    virtual bool IsCategoryEnabled(const char* category) { return true; }

    // Called by the trace engine to indicate it has completed startup.
    virtual void TraceStarted() {}

    // Called by the trace engine when tracing has stopped.
    //
    // The trace collection status is |ZX_OK| if trace collection was successful.
    // An error indicates that the trace data may be inaccurate or incomplete.
    //
    // |async| is the trace engine's asynchronous dispatcher.
    // |disposition| is |ZX_OK| if tracing stopped normally, otherwise indicates
    // that tracing was aborted due to an error. If records were dropped (due
    // to the trace buffer being full) then |disposition| is |ZX_ERR_NO_MEMORY|.
    // |buffer_bytes_written| is number of bytes which were written to the trace buffer.
    //
    // Called on an asynchronous dispatch thread.
    virtual void TraceStopped(async_dispatcher_t* dispatcher,
                              zx_status_t disposition, size_t buffer_bytes_written) {}

    // Called by the trace engine in streaming mode to indicate a buffer is full.
    // This is only used in streaming mode where double-buffering is used.
    // |wrapped_count| is the number of times writing to the buffer has
    // switched from one buffer to the other.
    // |durable_data_end| is the offset into the durable buffer when the
    // buffer filled. It is provided so that TraceManager can save the data
    // thus far written to the durable buffer.
    virtual void NotifyBufferFull(uint32_t wrapped_count, uint64_t durable_data_end) {}

private:
    static bool CallIsCategoryEnabled(trace_handler_t* handler, const char* category);
    static void CallTraceStarted(trace_handler_t* handler);
    static void CallTraceStopped(trace_handler_t* handler, async_dispatcher_t* dispatcher,
                                 zx_status_t disposition, size_t buffer_bytes_written);
    static void CallNotifyBufferFull(trace_handler_t* handler,
                                     uint32_t wrapped_count,
                                     uint64_t durable_data_end);

    static const trace_handler_ops_t kOps;
};

} // namespace trace

#endif // __cplusplus
