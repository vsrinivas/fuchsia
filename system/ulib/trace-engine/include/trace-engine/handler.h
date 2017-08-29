// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// The ABI-stable entry points used by trace instrumentation libraries.
//
// Trace handlers manage the configuration, lifecycle, and external communication
// of the trace engine.  The trace engine binds to a single trace handler for
// the duration of a trace.  During the trace, the trace engine invokes methods
// on the trace handler to ask about enabled categories and to report relevant
// state changes.
//
// Client code shouldn't be using these APIs directly.
// See <trace/event.h> for instrumentation macros.
//

#pragma once

#include <stdbool.h>

#include <magenta/compiler.h>
#include <magenta/types.h>

#include <async/dispatcher.h>
#include <trace-engine/instrumentation.h>

__BEGIN_CDECLS

// Trace handler interface.
//
// Implementations must supply valid function pointers for each function
// defined in the |ops| structure.
typedef struct trace_handler_ops trace_handler_ops_t;

typedef struct trace_handler {
    const trace_handler_ops_t* ops;
} trace_handler_t;

struct trace_handler_ops {
    // Called by the trace engine to ask whether the specified category is enabled.
    //
    // This method may be called frequently so it must be efficiently implemented.
    // Clients may cache the results while a trace is running; dynamic changes
    // to the enabled categories may go unnoticed until the next trace.
    //
    // |handler| is the trace handler object itself.
    // |category| is the name of the category.
    //
    // Called by instrumentation on any thread.  Must be thread-safe.
    bool (*is_category_enabled)(trace_handler_t* handler, const char* category);

    // Called by the trace engine when tracing has stopped.
    //
    // The trace collection status is |MX_OK| if trace collection was successful.
    // An error indicates that the trace data may be inaccurate or incomplete.
    //
    // |handler| is the trace handler object itself.
    // |async| is the trace engine's asynchronous dispatcher.
    // |disposition| is |MX_OK| if tracing stopped normally, otherwise indicates
    // that tracing was aborted due to an error.
    // |buffer_bytes_written| is number of bytes which were written to the trace buffer.
    //
    // Called on an asynchronous dispatch thread.
    void (*trace_stopped)(trace_handler_t* handler, async_t* async,
                          mx_status_t disposition, size_t buffer_bytes_written);
};

// Asynchronously starts the trace engine.
//
// |async| is the asynchronous dispatcher which the trace engine will use for dispatch.
// |handler| is the trace handler which will handle lifecycle events.
// |buffer| is the trace buffer into which the trace engine will write trace events.
// |buffer_num_bytes| is the size of the trace buffer in bytes.
//
// Returns |MX_OK| if tracing is ready to go.
// Returns |MX_ERR_BAD_STATE| if tracing is already in progress.
// Returns |MX_ERR_NO_MEMORY| if allocation failed.
//
// This function is thread-safe.
//
// NOTE: Asynchronous dispatcher shutdown behavior:
//
// The trace engine will attempt to stop itself automatically when the
// asynchronous dispatcher specified in |async| begins the process of shutting
// itself down (usually just prior to the dispatcher's destruction).  However,
// the trace engine may fail to come to a complete stop if there remain outstanding
// references to the trace context during dispatcher shutdown.  When this happens,
// the trace handler will not be notified of trace completion and subsequent calls
// to |trace_start_engine()| will return |MX_ERR_BAD_STATE|.
//
// For this reason, it is a good idea to call |trace_stop_engine()| and wait
// for the handler to receive the |trace_handler_ops.trace_stopped()| callback
// prior to shutting down the trace engine's asynchronous dispatcher.
//
// Better yet, don't shut down the trace engine's asynchronous dispatcher unless
// the process is already about to exit.
mx_status_t trace_start_engine(async_t* async,
                               trace_handler_t* handler,
                               void* buffer,
                               size_t buffer_num_bytes);

// Asynchronously stops the trace engine.
//
// The trace handler's |trace_stopped()| method will be invoked asynchronously
// when the trace engine transitions to the |TRACE_STOPPED| states.
//
// |disposition| is |MX_OK| if tracing is being stopped normally, otherwise indicates
// that tracing is being aborted due to an error.
//
// Returns |MX_OK| if the current state is |TRACE_STARTED| or |TRACE_STOPPING|.
// Returns |MX_ERR_BAD_STATE| if current state is |TRACE_STOPPED|.
//
// This function is thread-safe.
mx_status_t trace_stop_engine(mx_status_t disposition);

__END_CDECLS
