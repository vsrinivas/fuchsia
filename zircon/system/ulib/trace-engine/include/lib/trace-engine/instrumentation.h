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

#ifndef ZIRCON_SYSTEM_ULIB_LIB_TRACE_ENGINE_INSTRUMENTATION_H_
#define ZIRCON_SYSTEM_ULIB_LIB_TRACE_ENGINE_INSTRUMENTATION_H_

#include <stdbool.h>
#include <stdint.h>

#include <zircon/compiler.h>

#include <lib/trace-engine/context.h>

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
  // Any data attempted to be written will be discarded.
  // This enum doesn't distinguish between "stopped" and "terminated".
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
static inline bool trace_is_enabled(void) { return trace_state() != TRACE_STOPPED; }

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
// It is also important to release the context promptly to maintain proper
// operation in streaming mode: The buffer can't be saved until all writers
// have released their context.
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
// It is also important to release the context promptly to maintain proper
// operation in streaming mode: The buffer can't be saved until all writers
// have released their context.
//
// This function is equivalent to calling |trace_acquire_context()| to acquire
// the engine's context, then calling |trace_context_register_category_literal()|
// to check whether the specified category is enabled and register it in the
// string table.  It releases the context and returns NULL if the category
// is not enabled.
//
// |category_literal| must be a null-terminated static string constant.
// |out_ref| points to where the registered string reference should be returned.
//
// Returns a valid trace context if tracing is enabled for the specified category.
// Returns NULL otherwise.
//
// This function is thread-safe and lock-free.
trace_context_t* trace_acquire_context_for_category(const char* category_literal,
                                                    trace_string_ref_t* out_ref);

// Opaque type that is used to cache category enabled/disabled state.
// ["opaque" in the sense that client code must not touch it]
// The term "site" is used because it's relatively unique and because this type
// is generally used to record category state at TRACE_<event>() call sites.
typedef uintptr_t trace_site_state_t;
typedef struct {
  // "state" is intentionally non-descript
  trace_site_state_t state;
} trace_site_t;

// Same as |trace_acquire_context_for_category()| except includes an extra
// parameter to allow for caching of the category lookup.
//
// |category_literal| must be a null-terminated static string constant.
// |site_ptr| must point to a variable of static storage duration initialized
//   to zero. A static local variable at the call site of recording a trace
//   event is the normal solution. The caller must not touch the memory pointed
//   to by this value, it is for the sole use of the trace engine.
// |out_ref| points to where the registered string reference should be returned.
//
// Returns a valid trace context if tracing is enabled for the specified category.
// Returns NULL otherwise.
//
// This function is thread-safe and lock-free.
trace_context_t* trace_acquire_context_for_category_cached(const char* category_literal,
                                                           trace_site_t* site_ptr,
                                                           trace_string_ref_t* out_ref);

// Flush the cache built up by calls to
// |trace_acquire_context_for_category_cached()|.
//
// The trace engine maintains this cache, but there is one case where it
// needs help: When a DSO containing cache state is unloaded; that is the
// |site_ptr| argument to a call to
// |trace_acquire_context_for_category_cached()| points into the soon to be
// unloaded DSO.
// This is normally not a problem as |dlclose()| is basically a nop.
// However, should a DSO get physically unloaded then this function must be
// called before the DSO is unloaded. The actual unloading procedure must be:
// 1) Stop execution in the DSO.
// 2) Stop tracing.
// 3) Call |trace_engine_flush_category_cache()|.
// 4) Unload DSO.
// (1,2) can be done in either order.
//
// Returns ZX_OK on success.
// Returns ZX_ERR_BAD_STATE if the engine is not stopped.
//
// This function is thread-safe.
zx_status_t trace_engine_flush_category_cache(void);

// Releases a reference to the trace engine's context.
// Must balance a prior successful call to |trace_acquire_context()|
// or |trace_acquire_context_for_category()|.
//
// |context| must be a valid trace context reference.
//
// This function is thread-safe, never-fail, and lock-free.
void trace_release_context(trace_context_t* context);

// Acquires a reference to the trace engine's context, for prolonged use.
// This cannot be used to acquire the context for the purposes of writing to
// the trace buffer. Instead, this is intended for uses like the ktrace
// provider where it wishes to hold a copy of the context for the duration of
// the trace.
// Must be balanced by a call to |trace_release_prolonged_context()| when the
// result is non-NULL.
//
// This function is optimized to return quickly when tracing is not enabled.
//
// Trace engine shutdown is deferred until all references to the trace context
// have been released, therefore it is important for clients to promptly
// release their reference to the trace context once they have finished with
// it.
//
// Returns a valid trace context if tracing is enabled.
// Returns NULL otherwise.
//
// This function is thread-safe, fail-fast, and lock-free.
trace_prolonged_context_t* trace_acquire_prolonged_context(void);

// Releases a reference to the trace engine's prolonged context.
// Must balance a prior successful call to |trace_acquire_prolonged_context()|.
//
// |context| must be a valid trace context reference.
//
// This function is thread-safe, never-fail, and lock-free.
void trace_release_prolonged_context(trace_prolonged_context_t* context);

// Registers an event handle which the trace engine will signal when the
// trace state or set of enabled categories changes.
//
// Trace observers can use this mechanism to activate custom instrumentation
// mechanisms and write collected information into the trace buffer in response
// to state changes.
//
// The protocol works like this:
//
// 1. The trace observer creates an event object (using |zx_event_create()| or
//    equivalent) then calls |trace_register_observer()| to register itself.
// 2. The trace observer queries the current trace state and set of enabled categories.
// 3. If tracing is enabled, the trace observer configures itself to collect data
//    and write trace records relevant to the set of enabled categories.
// 4. When the trace state and/or set of enabled categories changes, the trace engine
//    sets the |ZX_EVENT_SIGNALED| signal bit of each |event| associated with
//    currently registered observers.
// 5. In response to observing the |ZX_EVENT_SIGNALED| signal, the trace observer
//    first clears the |ZX_EVENT_SIGNALED| bit (using |zx_object_signal()| or equivalent)
//    then adjusts its behavior as in step 2 and 3 above, and then calls
//    trace_notify_observer_updated().
// 6. When no longer interested in receiving events, the trace observer calls
//    |trace_unregister_observer()| to unregister itself then closes the event handle.
//
// Returns |ZX_OK| if successful.
// Returns |ZX_ERR_INVALID_ARGS| if the event was already registered.
zx_status_t trace_register_observer(zx_handle_t event);

// Unregisters the observer event handle previously registered with
// |trace_register_observer|.
//
// Returns |ZX_OK| if successful.
// Returns |ZX_ERR_NOT_FOUND| if the event was not previously registered.
zx_status_t trace_unregister_observer(zx_handle_t event);

// Callback to notify the engine that the observer has finished processing
// all state changes.
void trace_notify_observer_updated(zx_handle_t event);

__END_CDECLS

#ifdef __cplusplus

namespace trace {

// Holds and retains ownership of a trace context.
// Releases the context automatically when destroyed.
class TraceContext final {
 public:
  TraceContext() : context_(nullptr) {}

  TraceContext(trace_context_t* context) : context_(context) {}

  TraceContext(TraceContext&& other) : context_(other.context_) { other.context_ = nullptr; }

  ~TraceContext() { Release(); }

  // Gets the trace context, or null if there is none.
  trace_context_t* get() const { return context_; }

  // Returns true if the holder contains a valid context.
  explicit operator bool() const { return context_ != nullptr; }

  // Acquires a reference to the trace engine's context.
  static TraceContext Acquire() { return TraceContext(trace_acquire_context()); }

  // Acquires a reference to the trace engine's context, only if the specified
  // category is enabled.
  static TraceContext AcquireForCategory(const char* category_literal,
                                         trace_string_ref_t* out_ref) {
    return TraceContext(trace_acquire_context_for_category(category_literal, out_ref));
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

  TraceContext(const TraceContext&) = delete;
  TraceContext& operator=(const TraceContext&) = delete;
};

// Holds and retains ownership of a prolonged trace context.
// Releases the context automatically when destroyed.
class TraceProlongedContext final {
 public:
  TraceProlongedContext() : context_(nullptr) {}

  TraceProlongedContext(trace_prolonged_context_t* context) : context_(context) {}

  TraceProlongedContext(TraceProlongedContext&& other) : context_(other.context_) {
    other.context_ = nullptr;
  }

  ~TraceProlongedContext() { Release(); }

  // Gets the trace context, or null if there is none.
  trace_prolonged_context_t* get() const { return context_; }

  // Returns true if the holder contains a valid context.
  explicit operator bool() const { return context_ != nullptr; }

  // Acquires a reference to the trace engine's context.
  static TraceProlongedContext Acquire() {
    return TraceProlongedContext(trace_acquire_prolonged_context());
  }

  // Releases the trace context.
  void Release() {
    if (context_) {
      trace_release_prolonged_context(context_);
      context_ = nullptr;
    }
  }

  TraceProlongedContext& operator=(TraceProlongedContext&& other) {
    Release();
    context_ = other.context_;
    other.context_ = nullptr;
    return *this;
  }

 private:
  trace_prolonged_context_t* context_;

  TraceProlongedContext(const TraceProlongedContext&) = delete;
  TraceProlongedContext& operator=(const TraceProlongedContext&) = delete;
};

}  // namespace trace

#endif  // __cplusplus

#endif  // ZIRCON_SYSTEM_ULIB_LIB_TRACE_ENGINE_INSTRUMENTATION_H_
