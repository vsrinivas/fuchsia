// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// The ABI-stable entry points used by trace instrumentation libraries.
//
// Functions used by process-wide trace instrumentation to query the state
// of the trace engine and acquire the engine's trace context.
//
// The engine's trace context is initialized when the trace engine is started
// and is destroyed when the trace engine completely stops after all references
// have been released.
//
// Acquiring a reference to the engine's trace context is optimized for speed
// to be fail-fast and lock-free.  This helps to ensure that trace
// instrumentation has negligible performance impact when tracing is disabled
// (on the order of nanoseconds) and only a small impact when tracing is enabled
// (on the order of tens to hundreds of nanoseconds depending on the complexity
// of the trace records being written).
//
// Client code shouldn't be using these APIs directly.
// See <trace/event.h> for instrumentation macros.
//

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <magenta/compiler.h>

#include <trace-engine/context.h>

__BEGIN_CDECLS

// Returns a new unique 64-bit unsigned integer (within this process).
// Each invocation returns a new unique non-zero value.
//
// Useful for generating unique correlation ids for async and flow events.
//
// This function is thread-safe and lock-free.
uint64_t trace_generate_nonce(void);

// Describes the state of the trace engine.
typedef enum {
    // Trace instrumentation is inactive.
    // Any data written into the trace buffer will be discarded.
    TRACE_STOPPED = 0,
    // Trace instrumentation is active.
    TRACE_STARTED = 1,
    // Trace instrumentation is active but is in the process of shutting down.
    // Tracing will stop once all references to the trace buffer have been released.
    TRACE_STOPPING = 2,
} trace_state_t;

// Gets the current state of the trace engine.
//
// This function is thread-safe.
trace_state_t trace_state(void);

// Returns true if tracing is enabled (started or stopping but not stopped).
//
// This function is thread-safe and lock-free.
inline bool trace_is_enabled(void) {
    return trace_state() != TRACE_STOPPED;
}

// Returns true if tracing of the specified category has been enabled (which
// implies that |trace_is_enabled()| is also true).
//
// Use |trace_acquire_context_for_category()| if you intend to immediately
// write a record into the trace buffer after checking the category.
//
// |category_literal| must be a null-terminated static string constant.
//
// This function is thread-safe.
bool trace_is_category_enabled(const char* category_literal);

// Acquires a reference to the trace engine's context.
// Must be balanced by a call to |trace_release_context()| when the result is non-NULL.
//
// This function is optimized to return quickly when tracing is not enabled.
//
// Trace engine shutdown is deferred until all references to the trace context
// have been released, therefore it is important for clients to promptly
// release their reference to the trace context once they have finished
// writing records into the trace buffer.
//
// Returns a valid trace context if tracing is enabled.
// Returns NULL otherwise.
//
// This function is thread-safe, fail-fast, and lock-free.
trace_context_t* trace_acquire_context(void);

// Acquires a reference to the trace engine's context, only if the specified
// category is enabled.  Must be balanced by a call to |trace_release_context()|
// when the result is non-NULL.
//
// This function is optimized to return quickly when tracing is not enabled.
//
// Trace engine shutdown is deferred until all references to the trace context
// have been released, therefore it is important for clients to promptly
// release their reference to the trace context once they have finished
// writing records into the trace buffer.
//
// This function is equivalent to calling |trace_acquire_context()| to acquire
// the engine's context, then calling |trace_context_register_category_literal()|
// to check whether the specified category is enabled and register it in the
// string table.  It releases the context and returns NULL if the category
// is not enabled.
//
// |context| must be a valid trace context reference.
// |category_literal| must be a null-terminated static string constant.
// |out_ref| points to where the registered string reference should be returned.
//
// Returns a valid trace context if tracing is enabled for the specified category.
// Returns NULL otherwise.
//
// This function is thread-safe.
trace_context_t* trace_acquire_context_for_category(const char* category_literal,
                                                    trace_string_ref_t* out_ref);

// Releases a reference to the trace engine's context.
// Must balance a prior successful call to |trace_acquire_context()|
// or |trace_acquire_context_for_category()|.
//
// |context| must be a valid trace context reference.
//
// This function is thread-safe, never-fail, and lock-free.
void trace_release_context(trace_context_t* context);

// Registers an event handle which the trace engine will signal when the
// trace state or set of enabled categories changes.
//
// Trace observers can use this mechanism to activate custom instrumentation
// mechanisms and write collected information into the trace buffer in response
// to state changes.
//
// The protocol works like this:
//
// 1. The trace observer creates an event object (using |mx_event_create()| or
//    equivalent) then calls |trace_register_observer()| to register itself.
// 2. The trace observer queries the current trace state and set of enabled categories.
// 3. If tracing is enabled, the trace observer configures itself to collect data
//    and write trace records relevant to the set of enabled categories.
// 4. When the trace state and/or set of enabled categories changes, the trace engine
//    sets the |MX_EVENT_SIGNALED| signal bit of each |event| associated with
//    currently registered observers.
// 5. In response to observing the |MX_EVENT_SIGNALED| signal, the trace observer
//    first clears the |MX_EVENT_SIGNALED| bit (using |mx_object_signal()| or equivalent)
//    then adjusts its behavior as in step 2 and 3 above.
// 6. When no longer interested in receiving events, the trace observer calls
//    |trace_unregister_observer()| to unregister itself then closes the event handle.
//
// Returns |MX_OK| if successful.
// Returns |MX_ERR_INVALID_ARGS| if the event was already registered.
mx_status_t trace_register_observer(mx_handle_t event);

// Unregisters the observer event handle previously registered with
// |trace_register_observer|.
//
// Returns |MX_OK| if successful.
// Returns |MX_ERR_NOT_FOUND| if the event was not previously registered.
mx_status_t trace_unregister_observer(mx_handle_t event);

__END_CDECLS

#ifdef __cplusplus
#include <fbl/macros.h>

namespace trace {

// Holds and retains ownership of a trace context.
// Releases the context automatically when destroyed.
class TraceContext final {
public:
    TraceContext()
        : context_(nullptr) {}

    TraceContext(trace_context_t* context)
        : context_(context) {}

    TraceContext(TraceContext&& other)
        : context_(other.context_) {
        other.context_ = nullptr;
    }

    ~TraceContext() {
        Release();
    }

    // Gets the trace context, or null if there is none.
    trace_context_t* get() const { return context_; }

    // Returns true if the holder contains a valid context.
    explicit operator bool() const { return context_ != nullptr; }

    // Acquires a reference to the trace engine's context.
    static TraceContext Acquire() {
        return TraceContext(trace_acquire_context());
    }

    // Acquires a reference to the trace engine's context, only if the specified
    // category is enabled.
    static TraceContext AcquireForCategory(const char* category_literal,
                                           trace_string_ref_t* out_ref) {
        return TraceContext(trace_acquire_context_for_category(
            category_literal, out_ref));
    }

    // Releases the trace context.
    void Release() {
        if (context_) {
            trace_release_context(context_);
            context_ = nullptr;
        }
    }

    TraceContext& operator=(TraceContext&& other) {
        Release();
        context_ = other.context_;
        other.context_ = nullptr;
        return *this;
    }

private:
    trace_context_t* context_;

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(TraceContext);
};

} // namespace trace
#endif // __cplusplus
