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

#ifndef ZIRCON_SYSTEM_ULIB_LIB_TRACE_ENGINE_HANDLER_H_
#define ZIRCON_SYSTEM_ULIB_LIB_TRACE_ENGINE_HANDLER_H_

#include <stdbool.h>

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <lib/async/dispatcher.h>
#include <lib/trace-engine/instrumentation.h>

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

  // Called by the trace engine to indicate it has completed startup.
  void (*trace_started)(trace_handler_t* handler);

  // Called by the trace engine when tracing has stopped.
  //
  // The trace collection status is |ZX_OK| if trace collection was successful.
  // An error indicates that the trace data may be inaccurate or incomplete.
  //
  // |handler| is the trace handler object itself.
  // |disposition| is |ZX_OK| if tracing stopped normally, otherwise indicates
  // that tracing was aborted due to an error.
  //
  // Called on an asynchronous dispatch thread.
  void (*trace_stopped)(trace_handler_t* handler, zx_status_t disposition);

  // Called by the trace engine to indicate it has terminated.
  //
  // Called on an asynchronous dispatch thread.
  void (*trace_terminated)(trace_handler_t* handler);

  // Called by the trace engine after an attempt to allocate space
  // for a new record has failed because the buffer is full.
  //
  // Called by instrumentation on any thread.  Must be thread-safe.
  void (*notify_buffer_full)(trace_handler_t* handler, uint32_t wrapped_count,
                             uint64_t durable_data_end);

  // Called by the trace engine to send a trigger.
  //
  // Called by instrumentation on any thread.  Must be thread-safe.
  void (*send_trigger)(trace_handler_t* handler, const char* trigger_name);
};

// Whether to clear the trace buffer when starting the engine.
typedef enum {
  // The numbering here is chosen to match the |BufferDisposition| enum in
  // the fuchsia.tracing.provider.Provider FIDL protocol.
  TRACE_START_CLEAR_ENTIRE_BUFFER = 1,
  TRACE_START_CLEAR_NONDURABLE_BUFFER = 2,
  TRACE_START_RETAIN_BUFFER = 3,
} trace_start_mode_t;

// Initialize the trace engine.
//
// |async| is the asynchronous dispatcher which the trace engine will use for dispatch (borrowed).
// |handler| is the trace handler which will handle lifecycle events (borrowed).
// |buffer| is the trace buffer into which the trace engine will write trace events (borrowed).
// |buffer_num_bytes| is the size of the trace buffer in bytes.
//
// Returns |ZX_OK| if tracing is ready to go.
// Returns |ZX_ERR_BAD_STATE| if tracing has already been initialized.
// Returns |ZX_ERR_NO_MEMORY| if allocation failed.
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
// to |trace_engine_start()| will return |ZX_ERR_BAD_STATE|.
//
// For this reason, it is a good idea to call |trace_engine_terminate()| and wait
// for the handler to receive the |trace_handler_ops.trace_terminated()| callback
// prior to shutting down the trace engine's asynchronous dispatcher.
//
// Better yet, don't shut down the trace engine's asynchronous dispatcher unless
// the process is already about to exit.
zx_status_t trace_engine_initialize(async_dispatcher_t* dispatcher, trace_handler_t* handler,
                                    trace_buffering_mode_t buffering_mode, void* buffer,
                                    size_t buffer_num_bytes);

// Asynchronously starts the trace engine.
// The engine must have already be initialized with |trace_engine_initialize()|.
//
// |mode| specifies whether to clear the trace buffer first.
//
// Returns |ZX_OK| if tracing is ready to go.
// Returns |ZX_ERR_BAD_STATE| if tracing is already in progress.
//
// This function is thread-safe.
zx_status_t trace_engine_start(trace_start_mode_t mode);

// Asynchronously stops the trace engine.
//
// The trace handler's |trace_stopped()| method will be invoked asynchronously
// when the trace engine transitions to the |TRACE_STOPPED| state.
// Does nothing if tracing has already stopped.
//
// |disposition| is |ZX_OK| if tracing is being stopped normally, otherwise indicates
// that tracing is being aborted due to an error.
//
// This function is thread-safe.
void trace_engine_stop(zx_status_t disposition);

// Asynchronously terminates the trace engine.
//
// This must be called before tracing is initialized again.
//
// The trace handler's |trace_terminated()| method will be invoked asynchronously,
// after the trace engine transitions to the |TRACE_STOPPED| state if not already
// stopped.
// This may be called whether tracing is currenting started or not.
// Does nothing if tracing has already terminated.
//
// If tracing is not already stopped the disposition is set to |ZX_OK|.
// If a different disposition is desired, call |trace_engine_stop()| first.
//
// This function is thread-safe.
void trace_engine_terminate();

// Asynchronously notifies the engine that buffers up to |wrapped_count|
// have been saved.
//
// Returns |ZX_OK| if the current state is |TRACE_STARTED| or |TRACE_STOPPING|.
// Returns |ZX_ERR_BAD_STATE| if current state is |TRACE_STOPPED|.
//
// This function is thread-safe.
zx_status_t trace_engine_mark_buffer_saved(uint32_t wrapped_count, uint64_t durable_data_end);

__END_CDECLS

#endif  // ZIRCON_SYSTEM_ULIB_LIB_TRACE_ENGINE_HANDLER_H_
