// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trace-engine/handler.h>

#include <string.h>

#include <magenta/assert.h>

#include <async/wait.h>
#include <mx/event.h>
#include <fbl/atomic.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/vector.h>
#include <trace-engine/instrumentation.h>

#include "context_impl.h"

namespace {

// Amount of time to allow for other threads to release their references
// to the trace buffer during shutdown.  See point of use for details.
constexpr unsigned int kSynchronousShutdownTimeoutMilliseconds = 1000;

// Trace engine lock.
// See rules below for how this is used.
fbl::Mutex g_engine_mutex;

// Trace instrumentation state.
// Rules:
//   - can only be modified while holding g_engine_mutex
//   - can be read atomically at any time
fbl::atomic<int> g_state{TRACE_STOPPED};

// Trace disposition.
// This is the status that will be reported to the trace handler when the
// trace finishes.
// Rules:
//   - can only be accessed or modified while holding g_engine_mutex
mx_status_t g_disposition{MX_OK};

// Trace asynchronous dispatcher.
// Rules:
//   - can only be modified while holding g_engine_mutex and engine is stopped
//   - can be read outside the lock only while the engine is not stopped
async_t* g_async{nullptr};

// Trace handler.
// Rules:
//   - can only be modified while holding g_engine_mutex and engine is stopped
//   - can be read outside the lock only while the engine is not stopped
trace_handler_t* g_handler{nullptr};

// Trace observer table.
// Rules:
//   - can only be accessed or modified while holding g_engine_mutex
fbl::Vector<mx_handle_t> g_observers;

// Trace context reference count.
// This functions as a non-exclusive lock for the engine's trace context.
// Rules:
//   - acquiring a reference acts as an ACQUIRE fence
//   - releasing a reference acts as a RELEASE fence
//   - always 0 when engine stopped
//   - transition from 0 to 1 only happens when engine is started
//   - the engine stops when the reference count goes to 0
//     (in other words, holding a context reference prevents the engine from stopping)
fbl::atomic_uint32_t g_context_refs{0u};

// Trace context released event.
// Used by |trace_release_context()| to signal (with MX_EVENT_SIGNALED) when
// the trace context reference count has dropped to zero.
// Rules:
//   - can only be modified while holding g_engine_mutex and engine is stopped
//   - can be read outside the lock while the engine is not stopped
mx::event g_context_released_event;

// Trace context.
// Rules:
//   - can only be modified while holding g_engine_mutex and engine is stopped
//   - can be accessed outside the lock while holding a context reference
trace_context_t* g_context{nullptr};

// Asynchronous operations posted to the asynchronous dispatcher while the
// engine is running.  Use of these structures is guarded by the engine lock.
async_wait_t g_context_released_wait;

async_wait_result_t handle_context_released(async_t* async, async_wait_t* wait,
                                            mx_status_t status,
                                            const mx_packet_signal_t* signal);

// must hold g_engine_mutex
inline void update_disposition_locked(mx_status_t disposition) {
    if (g_disposition == MX_OK)
        g_disposition = disposition;
}

void notify_observers_locked() {
    for (mx_handle_t observer : g_observers) {
        mx_status_t status = mx_object_signal(observer, 0u, MX_EVENT_SIGNALED);
        MX_DEBUG_ASSERT(status == MX_OK);
    }
}

} // namespace

/*** Trace engine functions ***/

// thread-safe
mx_status_t trace_start_engine(async_t* async,
                               trace_handler_t* handler,
                               void* buffer,
                               size_t buffer_num_bytes) {
    MX_DEBUG_ASSERT(async);
    MX_DEBUG_ASSERT(handler);
    MX_DEBUG_ASSERT(buffer);

    fbl::AutoLock lock(&g_engine_mutex);

    // We must have fully stopped a prior tracing session before starting a new one.
    if (g_state.load(fbl::memory_order_relaxed) != TRACE_STOPPED)
        return MX_ERR_BAD_STATE;
    MX_DEBUG_ASSERT(g_context_refs.load(fbl::memory_order_relaxed) == 0u);

    mx::event context_released_event;
    mx_status_t status = mx::event::create(0u, &context_released_event);
    if (status != MX_OK)
        return status;

    // Schedule a wait for the buffer to be released.
    g_context_released_wait = {
        .state = {ASYNC_STATE_INIT},
        .handler = &handle_context_released,
        .object = context_released_event.get(),
        .trigger = MX_EVENT_SIGNALED,
        .flags = ASYNC_FLAG_HANDLE_SHUTDOWN,
        .reserved = 0};
    status = async_begin_wait(async, &g_context_released_wait);
    if (status != MX_OK)
        return status;

    // Initialize the trace engine state and context.
    g_state.store(TRACE_STARTED, fbl::memory_order_relaxed);
    g_async = async;
    g_handler = handler;
    g_disposition = MX_OK;
    g_context = new trace_context(buffer, buffer_num_bytes, handler);
    g_context_released_event = fbl::move(context_released_event);

    // Write the trace initialization record first before allowing clients to
    // get in and write their own trace records.
    trace_context_write_initialization_record(g_context, mx_ticks_per_second());

    // After this point clients can acquire references to the trace context.
    g_context_refs.store(1u, fbl::memory_order_release);

    // Notify observers that the state changed.
    notify_observers_locked();
    return MX_OK;
}

// thread-safe
mx_status_t trace_stop_engine(mx_status_t disposition) {
    fbl::AutoLock lock(&g_engine_mutex);

    // We must have have an active trace in order to stop it.
    int state = g_state.load(fbl::memory_order_relaxed);
    if (state == TRACE_STOPPED)
        return MX_ERR_BAD_STATE;

    update_disposition_locked(disposition);
    if (state == TRACE_STOPPING)
        return MX_OK; // already stopping

    MX_DEBUG_ASSERT(state == TRACE_STARTED);
    MX_DEBUG_ASSERT(g_context_refs.load(fbl::memory_order_relaxed) != 0u);

    // Begin stopping the trace.
    g_state.store(TRACE_STOPPING, fbl::memory_order_relaxed);

    // Notify observers that the state changed.
    notify_observers_locked();

    // Release the trace engine's own reference to the trace context.
    // |handle_context_released()| will be called asynchronously when the last
    // reference is released.
    trace_release_context(g_context);
    return MX_OK;
}

namespace {

async_wait_result_t handle_context_released(async_t* async, async_wait_t* wait,
                                            mx_status_t status,
                                            const mx_packet_signal_t* signal) {
    // Handle the case where the asynchronous dispatcher is being shut down.
    if (status != MX_OK) {
        MX_DEBUG_ASSERT(status == MX_ERR_CANCELED);

        // Stop the engine, in case it hasn't noticed yet.
        trace_stop_engine(MX_ERR_CANCELED);

        // There may still be outstanding references to the trace context.
        // We don't know when or whether they will be cleared but we can't complete
        // shut down until they are gone since there might still be live pointers
        // into the trace buffer so allow a brief timeout.  If the release event
        // hasn't been signaled by then, declare the trace engine dead in the water
        // to prevent dangling pointers.  This situations should be very rare as it
        // only occurs when the asynchronous dispatcher is shutting down, typically
        // just prior to process exit.
        status = g_context_released_event.wait_one(
            MX_EVENT_SIGNALED,
            mx_deadline_after(MX_MSEC(kSynchronousShutdownTimeoutMilliseconds)),
            nullptr);
        if (status != MX_OK) {
            // Uh oh.
            printf("Timed out waiting for %u trace context references to be released "
                   "after %u ms while the asynchronous dispatcher was shutting down.\n"
                   "Tracing will no longer be available in this process.",
                   g_context_refs.load(fbl::memory_order_relaxed),
                   kSynchronousShutdownTimeoutMilliseconds);
            return ASYNC_WAIT_FINISHED;
        }
    }

    // All ready to clean up.
    // Grab the mutex while modifying shared state.
    mx_status_t disposition;
    trace_handler_t* handler;
    size_t buffer_bytes_written;
    {
        fbl::AutoLock lock(&g_engine_mutex);

        MX_DEBUG_ASSERT(g_state.load(fbl::memory_order_relaxed) == TRACE_STOPPING);
        MX_DEBUG_ASSERT(g_context_refs.load(fbl::memory_order_relaxed) == 0u);
        MX_DEBUG_ASSERT(g_context != nullptr);

        // Get final disposition.
        if (g_context->is_buffer_full())
            update_disposition_locked(MX_ERR_NO_MEMORY);
        disposition = g_disposition;
        handler = g_handler;
        buffer_bytes_written = g_context->bytes_allocated();

        // Tidy up.
        g_async = nullptr;
        g_handler = nullptr;
        g_disposition = MX_OK;
        g_context_released_event.reset();
        delete g_context;
        g_context = nullptr;

        // After this point, it's possible for the engine to be restarted.
        g_state.store(TRACE_STOPPED, fbl::memory_order_relaxed);
    }

    // Notify the handler about the final disposition.
    handler->ops->trace_stopped(handler, async, disposition, buffer_bytes_written);
    return ASYNC_WAIT_FINISHED;
}

} // namespace

/*** Trace instrumentation functions ***/

// thread-safe, lock-free
trace_state_t trace_state() {
    return static_cast<trace_state_t>(g_state.load(fbl::memory_order_relaxed));
}

// thread-safe
bool trace_is_category_enabled(const char* category_literal) {
    trace_context_t* context = trace_acquire_context();
    if (likely(!context))
        return false;
    bool result = trace_context_is_category_enabled(context, category_literal);
    trace_release_context(context);
    return result;
}

// thread-safe, fail-fast, lock-free
trace_context_t* trace_acquire_context() {
    // Fail fast: Check whether we could possibly write into the trace buffer.
    // The count must be at least 1 to indicate that the buffer is initialized.
    // This is marked likely because tracing is usually disabled and we want
    // to return as quickly as possible from this function.
    uint32_t count = g_context_refs.load(fbl::memory_order_relaxed);
    if (likely(count == 0u))
        return nullptr;

    // Attempt to increment the reference count.
    // This also acts as a fence for future access to buffer state variables.
    //
    // Note the ACQUIRE fence here since the trace context may have changed
    // from the perspective of this thread.
    while (!g_context_refs.compare_exchange_weak(&count, count + 1,
                                                 fbl::memory_order_acquire,
                                                 fbl::memory_order_relaxed)) {
        if (unlikely(count == 0u))
            return nullptr;
    }
    return g_context;
}

trace_context_t* trace_acquire_context_for_category(const char* category_literal,
                                                    trace_string_ref_t* out_ref) {
    // This is marked likely because tracing is usually disabled and we want
    // to return as quickly as possible from this function.
    trace_context_t* context = trace_acquire_context();
    if (likely(!context))
        return nullptr;

    if (!trace_context_register_category_literal(context, category_literal, out_ref)) {
        trace_release_context(context);
        return nullptr;
    }

    return context;
}

// thread-safe, never-fail, lock-free
void trace_release_context(trace_context_t* context) {
    MX_DEBUG_ASSERT(context == g_context);
    MX_DEBUG_ASSERT(g_context_refs.load(fbl::memory_order_relaxed) != 0u);

    // Note the RELEASE fence here since the trace context and trace buffer
    // contents may have changes from the perspective of other threads.
    if (unlikely(g_context_refs.fetch_sub(1u, fbl::memory_order_release) == 1u)) {
        // Notify the engine that the last reference was released.
        mx_status_t status = g_context_released_event.signal(0u, MX_EVENT_SIGNALED);
        MX_DEBUG_ASSERT(status == MX_OK);
    }
}

mx_status_t trace_register_observer(mx_handle_t event) {
    fbl::AutoLock lock(&g_engine_mutex);

    for (auto item : g_observers) {
        if (item == event)
            return MX_ERR_INVALID_ARGS;
    }

    g_observers.push_back(event);
    return MX_OK;
}

mx_status_t trace_unregister_observer(mx_handle_t event) {
    fbl::AutoLock lock(&g_engine_mutex);

    for (size_t i = 0; i < g_observers.size(); i++) {
        if (g_observers[i] == event) {
            g_observers.erase(i);
            return MX_OK;
        }
    }
    return MX_ERR_NOT_FOUND;
}
