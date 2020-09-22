// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/trace-engine/handler.h>
#include <lib/trace-engine/instrumentation.h>
#include <lib/zx/event.h>
#include <stdio.h>
#include <string.h>
#include <zircon/assert.h>

#include <atomic>
#include <utility>
#include <vector>

#include "context_impl.h"

namespace {

// Amount of time to allow for other threads to release their references
// to the trace buffer during shutdown.  See point of use for details.
constexpr zx::duration kSynchronousShutdownTimeout = zx::msec(1000);

// Trace engine lock.
// See rules below for how this is used.
std::mutex g_engine_mutex;

// Trace instrumentation state.
// Rules:
//   - can only be modified while holding g_engine_mutex
//   - can be read atomically at any time
std::atomic<int> g_state{TRACE_STOPPED};

// Trace disposition.
// This is the status that will be reported to the trace handler when the
// trace finishes.
// Rules:
//   - can only be accessed or modified while holding g_engine_mutex
zx_status_t g_disposition __TA_GUARDED(g_engine_mutex){ZX_OK};

// Trace asynchronous dispatcher.
// Rules:
//   - can only be modified while holding g_engine_mutex and engine is stopped
//   - can be read outside the lock only while the engine is not stopped
async_dispatcher_t* g_dispatcher{nullptr};

// Trace handler.
// Rules:
//   - can only be modified while holding g_engine_mutex and engine is stopped
//   - can be read outside the lock only while the engine is not stopped
trace_handler_t* g_handler{nullptr};

// Set to true when a trace is terminated and writes are in flight.
// Rules:
//   - can only be accessed or modified while holding g_engine_mutex
bool g_trace_terminated{false};

// Trace observer table.
// Rules:
//   - can only be accessed or modified while holding g_engine_mutex
struct Observer {
  // The event handle that we notify the observer through.
  zx_handle_t event;
  // Set to true when the engine starts to indicate we're waiting for this
  // observer to call us back, via |trace_notify_observer_updated()|, that
  // it has started. When it does call us back this is set back to false.
  bool awaiting_update_after_start;
};
std::vector<Observer> g_observers __TA_GUARDED(g_engine_mutex);

// Trace context reference count.
// This functions as a non-exclusive lock for the engine's trace context.
// Rules:
//   - acquiring a reference acts as an ACQUIRE fence
//   - releasing a reference acts as a RELEASE fence
//   - always 0 when engine stopped
//   - transition from 0 to non-zero only happens when engine is started
//   - the engine stops when the reference count goes to 0
//     (in other words, holding a context reference prevents the engine from stopping)
//
// There are two separate counters here that collectively provide the full
// count: buffer acquisitions and prolonged acquisitions. Buffer acquisitions
// are for the purpose of writing to the trace buffer. Prolonged acquisitions
// are for things like adhoc trace providers where they want to maintain a
// reference to the context for the duration of the trace.
// Buffer acquisitions increment/decrement the count by
// |kBufferCounterIncrement|. Prolonged acquisitions increment/decrement the
// count by |kProlongedCounterIncrement|.
// To maintain the property that the full count only transitions from 0 to 1
// when the engine is started |kProlongedCounterIncrement| == 1.
std::atomic_uint32_t g_context_refs{0u};

// The uint32_t context ref count is split this way:
// |31 ... 8| = buffer acquisition count
// |7 ... 0| = prolonged acquisition count
// There are generally only a handful of prolonged acquisitions. The code will
// assert-fail if there are more. This allows for 2^24 buffer acquisitions
// which is basically 2^24 threads. The values are also chosen so that the
// full count is easily interpreted when printed in hex.
constexpr uint32_t kProlongedCounterShift = 0;
constexpr uint32_t kProlongedCounterIncrement = 1 << kProlongedCounterShift;
constexpr uint32_t kMaxProlongedCounter = 127;
constexpr uint32_t kProlongedCounterMask = 0xff;
constexpr uint32_t kBufferCounterShift = 8;
constexpr uint32_t kBufferCounterIncrement = 1 << kBufferCounterShift;
constexpr uint32_t kBufferCounterMask = 0xffffff00;

// Trace context.
// Rules:
//   - can only be modified while holding g_engine_mutex and engine is stopped
//   - can be accessed outside the lock while holding a context reference
trace_context_t* g_context{nullptr};

// Event for tracking:
// - when all observers has started
//   (SIGNAL_ALL_OBSERVERS_STARTED)
// - when the trace context reference count has dropped to zero
//   (SIGNAL_CONTEXT_RELEASED)
// Rules:
//   - can only be modified while holding g_engine_mutex and engine is stopped
//   - can be read outside the lock while the engine is not stopped
zx::event g_event;
constexpr zx_signals_t SIGNAL_ALL_OBSERVERS_STARTED = ZX_USER_SIGNAL_0;
constexpr zx_signals_t SIGNAL_CONTEXT_RELEASED = ZX_USER_SIGNAL_1;

// Asynchronous operations posted to the asynchronous dispatcher while the
// engine is running.  Use of these structures is guarded by the engine lock.
async_wait_t g_event_wait;

inline uint32_t get_prolonged_context_refs(uint32_t raw) {
  return (raw & kProlongedCounterMask) >> kProlongedCounterShift;
}

inline uint32_t get_buffer_context_refs(uint32_t raw) {
  return (raw & kBufferCounterMask) >> kBufferCounterShift;
}

void handle_event(async_dispatcher_t* dispatcher, async_wait_t* wait, zx_status_t status,
                  const zx_packet_signal_t* signal);

// must hold g_engine_mutex
inline void update_disposition_locked(zx_status_t disposition) __TA_REQUIRES(g_engine_mutex) {
  if (g_disposition == ZX_OK)
    g_disposition = disposition;
}

void notify_observers_locked() __TA_REQUIRES(g_engine_mutex) {
  for (auto& observer : g_observers) {
    zx_status_t status = zx_object_signal(observer.event, 0u, ZX_EVENT_SIGNALED);
    ZX_DEBUG_ASSERT(status == ZX_OK);
  }
}

void notify_engine_all_observers_started_if_needed_locked() __TA_REQUIRES(g_engine_mutex) {
  for (auto& item : g_observers) {
    if (item.awaiting_update_after_start)
      return;
  }
  g_event.signal(0u, SIGNAL_ALL_OBSERVERS_STARTED);
}

// Table of per-call-site cached category enabled/disabled flags.
// This is done by chaining all the
// |trace_acquire_context_for_category_cached()| call sites at runtime,
// and recording with the pointer the enabled/disabled flag.
//
// Operation:
// 1. When tracing starts each value is zero (kSiteStateUnknown).
//    The value is generally a static local at the call site.
//    Note that while tracing was off various call sites may have been cached,
//    they are all reset to zero.
// 2. When a TRACE_*() macro is called, it calls
//    trace_acquire_context_for_category_cached().
// 3. If the DISABLED bit is set, skip, we're done.
// 4. Call trace_acquire_context_for_category()
// 5. If the ENABLED bit is set, return, we're done.
// 6. Insert the call site to the head of the chain with the
//    enabled/disabled bits set appropriately.
// 7. When tracing stops, empty the list. This includes resetting all chained
//    values to "unknown". We know they're actually disabled, but the important
//    part here is to flush the cache. A minor improvement would be to keep
//    the current list.
//    This is done both when the state transitions to STOPPING and again when
//    the state transitions to STOPPED.
// 8. When tracing starts again, reset all chained values to "unknown" and
//    flush the cache.
//
// The trick is doing this in as lock-free way as possible.
// Atomics are used for accessing the static local at the call site, and when
// the list needs to be traversed it is first atomically unchained from the
// main list and then operated on.
// Generally there aren't that many call sites, and we only need to traverse
// the list at trace start/stop time; so using a list isn't that much of a
// performance issue.

using trace_site_atomic_state_t = std::atomic<trace_site_state_t>;

// A sentinel is used so that there is no ambiguity between a null value
// being the end of the chain and a null value being the initial value of
// a chain slot.
trace_site_t g_site_cache_sentinel{};

std::atomic<trace_site_t*> g_site_cache{&g_site_cache_sentinel};

// Extra bits that are combined with the chain pointer to provide
// the full state.
constexpr trace_site_state_t kSiteStateUnknown = 0u;
constexpr trace_site_state_t kSiteStateDisabled = 1u;
constexpr trace_site_state_t kSiteStateEnabled = 2u;
constexpr trace_site_state_t kSiteStateFlagsMask = 3u;
// We don't export this value to the API, the API just says these values
// must be initialized to zero.
static_assert(kSiteStateUnknown == 0u);

// For clarity when reading the source.
using trace_site_flags_t = trace_site_state_t;

trace_site_state_t get_trace_site_raw_successor(trace_site_state_t state) {
  return state & ~kSiteStateFlagsMask;
}

trace_site_t* get_trace_site_successor(trace_site_state_t state) {
  return reinterpret_cast<trace_site_t*>(get_trace_site_raw_successor(state));
}

trace_site_flags_t get_trace_site_flags(trace_site_state_t state) {
  return state & kSiteStateFlagsMask;
}

trace_site_atomic_state_t* get_trace_site_state_as_atomic(trace_site_t* site) {
  return reinterpret_cast<trace_site_atomic_state_t*>(&site->state);
}

trace_site_state_t make_trace_site_state(trace_site_state_t successor, trace_site_flags_t flags) {
  return successor | flags;
}

trace_site_state_t make_trace_site_state(trace_site_t* successor, trace_site_flags_t flags) {
  return reinterpret_cast<trace_site_state_t>(successor) | flags;
}

trace_site_t* unchain_site_cache() {
  trace_site_t* empty_cache = &g_site_cache_sentinel;
  return g_site_cache.exchange(empty_cache, std::memory_order_relaxed);
}

void flush_site_cache() {
  // Atomically swap in an empty cache with the current one.
  trace_site_t* chain_head = unchain_site_cache();

  trace_site_t* chain = chain_head;
  while (chain != &g_site_cache_sentinel) {
    trace_site_atomic_state_t* state_ptr = get_trace_site_state_as_atomic(chain);
    trace_site_state_t curr_state = state_ptr->load(std::memory_order_relaxed);
    trace_site_state_t new_state = kSiteStateUnknown;
    state_ptr->store(new_state, std::memory_order_relaxed);
    chain = get_trace_site_successor(curr_state);
  }
}

// Update the state at |*site|.
// Note that multiple threads may race here for the same site.
void add_to_site_cache(trace_site_t* site, trace_site_state_t current_state, bool enabled) {
  trace_site_atomic_state_t* state_ptr = get_trace_site_state_as_atomic(site);

  // Even when tracing is on generally only a subset of categories
  // are traced, so the test uses "unlikely".
  trace_site_flags_t new_flags;
  if (unlikely(enabled)) {
    new_flags = kSiteStateEnabled;
  } else {
    new_flags = kSiteStateDisabled;
  }

  // At this point the recorded flags are zero. If we're the first to set
  // them then we're good to add our entry to the cache (if not already in
  // the cache). Otherwise punt. Note that this first setting of the flags
  // won't be the last if we need to also chain this entry into the cache.
  ZX_DEBUG_ASSERT(get_trace_site_flags(current_state) == kSiteStateUnknown);

  trace_site_state_t new_state =
      make_trace_site_state(get_trace_site_raw_successor(current_state), new_flags);
  // If someone else changed our state punt. This can happen when another
  // thread is tracing and gets there first.
  if (unlikely(!state_ptr->compare_exchange_strong(
          current_state, new_state, std::memory_order_acquire, std::memory_order_relaxed))) {
    return;
  }

  if (get_trace_site_raw_successor(new_state)) {
    // Already in chain.
    return;
  }

  // Add to chain.
  trace_site_t* old_cache_ptr = g_site_cache.load(std::memory_order_relaxed);
  new_state = make_trace_site_state(old_cache_ptr, new_flags);
  state_ptr->store(new_state, std::memory_order_relaxed);

  // Atomically update both:
  // - |g_site_cache| to point to |new_cache_ptr| (which is our entry)
  // - |*state_ptr| (our entry) to point to the old |g_site_cache|
  // This works because until our entry is live only its flag values
  // matter to other threads. See the discussion in |trace_engine_stop()|.
  trace_site_t* new_cache_ptr = site;
  while (!g_site_cache.compare_exchange_weak(
      old_cache_ptr, new_cache_ptr, std::memory_order_relaxed, std::memory_order_relaxed)) {
    // Someone else updated |g_site_cache|. Reset our chain pointer
    // and try again.
    new_state = make_trace_site_state(old_cache_ptr, new_flags);
    state_ptr->store(new_state, std::memory_order_relaxed);
  }
}

}  // namespace

/*** Trace engine functions ***/

// thread-safe
EXPORT_NO_DDK zx_status_t trace_engine_initialize(async_dispatcher_t* dispatcher,
                                                  trace_handler_t* handler,
                                                  trace_buffering_mode_t buffering_mode,
                                                  void* buffer, size_t buffer_num_bytes) {
  ZX_DEBUG_ASSERT(dispatcher);
  ZX_DEBUG_ASSERT(handler);
  ZX_DEBUG_ASSERT(buffer);

  switch (buffering_mode) {
    case TRACE_BUFFERING_MODE_ONESHOT:
    case TRACE_BUFFERING_MODE_CIRCULAR:
    case TRACE_BUFFERING_MODE_STREAMING:
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }

  // The buffer size must be a multiple of 4096 (simplifies buffer size
  // calcs).
  if ((buffer_num_bytes & 0xfff) != 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (buffer_num_bytes < trace_context::min_buffer_size() ||
      buffer_num_bytes > trace_context::max_buffer_size()) {
    return ZX_ERR_INVALID_ARGS;
  }

  std::lock_guard<std::mutex> lock(g_engine_mutex);

  // We must have fully terminated a prior tracing session before starting a new one.
  if (g_handler) {
    return ZX_ERR_BAD_STATE;
  }
  ZX_DEBUG_ASSERT(g_state.load(std::memory_order_relaxed) == TRACE_STOPPED);
  ZX_DEBUG_ASSERT(g_context_refs.load(std::memory_order_relaxed) == 0u);

  zx::event event;
  zx_status_t status = zx::event::create(0u, &event);
  if (status != ZX_OK) {
    return status;
  }

  // Initialize the trace engine state and context.
  // Note that we're still stopped at this point.
  g_dispatcher = dispatcher;
  g_handler = handler;
  g_disposition = ZX_OK;
  g_context = new trace_context(buffer, buffer_num_bytes, buffering_mode, handler);
  g_event = std::move(event);
  g_trace_terminated = false;

  g_context->ClearEntireBuffer();

  // Write the trace initialization record in case |trace_engine_start()| is
  // called with |TRACE_START_RETAIN_BUFFER|.
  trace_context_write_initialization_record(g_context, zx_ticks_per_second());

  return ZX_OK;
}

// thread-safe
EXPORT_NO_DDK zx_status_t trace_engine_start(trace_start_mode_t start_mode) {
  std::lock_guard<std::mutex> lock(g_engine_mutex);

  switch (start_mode) {
    case TRACE_START_CLEAR_ENTIRE_BUFFER:
    case TRACE_START_CLEAR_NONDURABLE_BUFFER:
    case TRACE_START_RETAIN_BUFFER:
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }

  // The engine must be initialized first.
  if (g_handler == nullptr) {
    // The provider library should have initialized us first.
    // Use ZX_ERR_INTERNAL to distinguish this from the "not stopped"
    // error: the FIDL provider protocol specifies that the response for
    // the latter error is to ignore the error. We leave it to the caller
    // to decide what to do with this error.
    return ZX_ERR_INTERNAL;
  }
  // |g_handler,g_context| are set/reset together.
  ZX_DEBUG_ASSERT(g_context != nullptr);

  // We must have fully stopped a prior tracing session before starting a new one.
  if (g_state.load(std::memory_order_relaxed) != TRACE_STOPPED) {
    return ZX_ERR_BAD_STATE;
  }
  ZX_DEBUG_ASSERT(g_context_refs.load(std::memory_order_relaxed) == 0u);

  // Schedule a waiter for |event|.
  g_event_wait = {.state = {ASYNC_STATE_INIT},
                  .handler = &handle_event,
                  .object = g_event.get(),
                  .trigger = (SIGNAL_ALL_OBSERVERS_STARTED | SIGNAL_CONTEXT_RELEASED),
                  .options = 0};
  zx_status_t status = async_begin_wait(g_dispatcher, &g_event_wait);
  if (status != ZX_OK) {
    return status;
  }

  // Initialize the trace engine state and context.
  g_state.store(TRACE_STARTED, std::memory_order_relaxed);

  switch (start_mode) {
    case TRACE_START_CLEAR_ENTIRE_BUFFER:
      g_context->ClearEntireBuffer();
      trace_context_write_initialization_record(g_context, zx_ticks_per_second());
      break;
    case TRACE_START_CLEAR_NONDURABLE_BUFFER:
      // Internal the "nondurable" buffer consists of the "rolling" buffers.
      g_context->ClearRollingBuffers();
      trace_context_write_initialization_record(g_context, zx_ticks_per_second());
      break;
    case TRACE_START_RETAIN_BUFFER:
      // Nothing to do.
      break;
    default:
      __UNREACHABLE;
  }

  // After this point clients can acquire references to the trace context.
  g_context_refs.store(kProlongedCounterIncrement, std::memory_order_release);

  // Flush the call-site cache.
  // Do this after clients can acquire the trace context so that any cached
  // values that got recorded prior to this are reset, and any new values
  // from this point on will see that tracing is on.
  flush_site_cache();

  // Notify observers that the state changed.
  if (g_observers.empty()) {
    g_event.signal(0u, SIGNAL_ALL_OBSERVERS_STARTED);
  } else {
    for (auto& observer : g_observers)
      observer.awaiting_update_after_start = true;
    notify_observers_locked();
  }

  return ZX_OK;
}

namespace {

void trace_engine_stop_locked(zx_status_t disposition) __TA_REQUIRES(g_engine_mutex) {
  // We must have an active trace in order to stop it.
  int state = g_state.load(std::memory_order_relaxed);
  if (state == TRACE_STOPPED) {
    return;
  }

  update_disposition_locked(disposition);
  if (state == TRACE_STOPPING) {
    // already stopping
    return;
  }

  ZX_DEBUG_ASSERT(state == TRACE_STARTED);
  ZX_DEBUG_ASSERT(g_context_refs.load(std::memory_order_relaxed) != 0u);

  // Begin stopping the trace.
  g_state.store(TRACE_STOPPING, std::memory_order_relaxed);

  // Flush the call-site cache.
  // Do this after tracing is marked as stopping so that any cached
  // values that got recorded prior to this are reset, and any new
  // values from this point on will see that tracing is stopping.
  // It's still possible that a cached value could be in the process of
  // being recorded as being enabled. So we might reset the site's state
  // and then it gets subsequently marked as enabled by another thread.
  // This is perhaps clumsy but ok: If the site got marked as enabled then a
  // trace context was acquired and engine state cannot change to STOPPED
  // until that context is released after which we will reset the state back
  // to disabled.
  flush_site_cache();

  // Notify observers that the state changed.
  notify_observers_locked();

  // Release the trace engine's own reference to the trace context.
  // |handle_context_released()| will be called asynchronously when the last
  // reference is released.
  trace_release_prolonged_context(reinterpret_cast<trace_prolonged_context_t*>(g_context));
}

void trace_engine_terminate_locked() __TA_REQUIRES(g_engine_mutex) {
  ZX_DEBUG_ASSERT(g_state.load(std::memory_order_relaxed) == TRACE_STOPPED);
  ZX_DEBUG_ASSERT(g_context_refs.load(std::memory_order_relaxed) == 0u);
  ZX_DEBUG_ASSERT(g_context != nullptr);
  ZX_DEBUG_ASSERT(g_handler != nullptr);

  delete g_context;
  g_context = nullptr;
  g_dispatcher = nullptr;
  g_handler = nullptr;
  g_event.reset();
}

}  // namespace

// thread-safe
EXPORT_NO_DDK void trace_engine_stop(zx_status_t disposition) {
  std::lock_guard<std::mutex> lock(g_engine_mutex);
  trace_engine_stop_locked(disposition);
}

// thread-safe
EXPORT_NO_DDK void trace_engine_terminate() {
  trace_handler_t* handler = nullptr;

  {
    std::lock_guard<std::mutex> lock(g_engine_mutex);

    // If trace is still running, stop it.
    // We must have an active trace in order to stop it.
    if (g_state.load(std::memory_order_relaxed) == TRACE_STOPPED) {
      if (g_handler == nullptr) {
        // Already terminated.
        return;
      }
      handler = g_handler;
      trace_engine_terminate_locked();
    } else {
      // Final termination has to wait for completion of all pending writers.
      g_trace_terminated = true;
      trace_engine_stop_locked(ZX_OK);
    }
  }

  if (handler) {
    handler->ops->trace_terminated(handler);
  }
}

// This is an internal function, only called from context.cpp.
// thread-safe
bool trace_engine_is_buffer_context_released() {
  return (g_context_refs.load(std::memory_order_relaxed) & kBufferCounterMask) == 0;
}

// This is an internal function, only called from context.cpp.
// thread-safe
void trace_engine_request_save_buffer(uint32_t wrapped_count, uint64_t durable_data_end) {
  // Handle the request on the engine's async loop. This may be get called
  // while servicing a client trace request, and we don't want to handle it
  // there.
  async::PostTask(g_dispatcher, [wrapped_count, durable_data_end]() {
    auto context = trace_acquire_prolonged_context();
    if (context) {
      auto tcontext = reinterpret_cast<trace_context_t*>(context);
      tcontext->HandleSaveRollingBufferRequest(wrapped_count, durable_data_end);
      trace_release_prolonged_context(context);
    }
  });
}

// This is called by the handler after it has saved a buffer.
// |wrapped_count| and |durable_end| are the values that were passed to it,
// and are passed back to us for sanity checking purposes.
// thread-safe
EXPORT_NO_DDK zx_status_t trace_engine_mark_buffer_saved(uint32_t wrapped_count,
                                                         uint64_t durable_data_end) {
  auto context = trace_acquire_prolonged_context();

  // No point in updating if there's no active trace.
  if (!context) {
    return ZX_ERR_BAD_STATE;
  }

  // Do this now, instead of as a separate iteration on the async loop.
  // The concern is that we want to update buffer state ASAP to reduce the
  // window where records might be dropped because the buffer is full.
  auto tcontext = reinterpret_cast<trace_context_t*>(context);
  tcontext->MarkRollingBufferSaved(wrapped_count, durable_data_end);

  trace_release_prolonged_context(context);
  return ZX_OK;
}

namespace {

void handle_all_observers_started() {
  // TODO(fxbug.dev/22873): Allow indicating an observer failed to start.

  // Clear the signal, otherwise we'll keep getting called.
  g_event.signal(SIGNAL_ALL_OBSERVERS_STARTED, 0u);

  // Note: There's no race in the use of |g_handler| here. If it will be set
  // to NULL that will be done later (handle_context_released is called by
  // handle_event after we are).
  if (g_handler) {
    g_handler->ops->trace_started(g_handler);
  }
}

void handle_context_released() {
  // All ready to clean up.
  // Grab the mutex while modifying shared state.
  zx_status_t disposition;
  trace_handler_t* handler;
  bool trace_terminated = false;

  {
    std::lock_guard<std::mutex> lock(g_engine_mutex);

    ZX_DEBUG_ASSERT(g_state.load(std::memory_order_relaxed) == TRACE_STOPPING);
    ZX_DEBUG_ASSERT(g_context_refs.load(std::memory_order_relaxed) == 0u);
    ZX_DEBUG_ASSERT(g_context != nullptr);

    // Update final buffer state.
    g_context->UpdateBufferHeaderAfterStopped();

    // Get final disposition.
    if (g_context->WasRecordDropped())
      update_disposition_locked(ZX_ERR_NO_MEMORY);
    disposition = g_disposition;
    // If we're also terminating |g_handler| will get reset.
    handler = g_handler;
    ZX_DEBUG_ASSERT(handler != nullptr);

    // Tidy up.
    g_disposition = ZX_OK;

    // Clear the signal, otherwise we'll keep getting called.
    g_event.signal(SIGNAL_CONTEXT_RELEASED, 0u);

    // After this point, it's possible for the engine to be restarted
    // (once we release the lock).
    g_state.store(TRACE_STOPPED, std::memory_order_relaxed);

    // Flush the call-site cache.
    // Do this after tracing is marked as stopped so that any cached
    // values that got recorded prior to this are reset, and any new
    // values from this point on will see that tracing is stopped.
    // Any call sites already chained are ok, the concern is with the
    // timing of call sites about to be added to the chain. We're ok
    // here because at this point it's impossible to acquire a trace
    // context, and therefore it's impossible for a category to be
    // cached as enabled.
    flush_site_cache();

    // If tracing has also terminated, finish processing that too.
    if (g_trace_terminated) {
      trace_terminated = true;
      trace_engine_terminate_locked();
    }

    // Notify observers that the state changed.
    notify_observers_locked();
  }

  // Handler operations are called outside the engine lock.

  // Notify the handler about the final disposition.
  handler->ops->trace_stopped(handler, disposition);

  if (trace_terminated) {
    handler->ops->trace_terminated(handler);
  }
}

// Handles the case where the asynchronous dispatcher has encountered an error
// and will no longer be servicing the wait callback.  Consequently, this is
// our last chance to stop the engine and await for all contexts to be released.
void handle_hard_shutdown(async_dispatcher_t* dispatcher) {
  // Stop the engine, in case it hasn't noticed yet.
  trace_engine_stop(ZX_ERR_CANCELED);
  // And terminate it.
  trace_engine_terminate();

  // There may still be outstanding references to the trace context.
  // We don't know when or whether they will be cleared but we can't complete
  // shut down until they are gone since there might still be live pointers
  // into the trace buffer so allow a brief timeout.  If the release event
  // hasn't been signaled by then, declare the trace engine dead in the water
  // to prevent dangling pointers.  This situations should be very rare as it
  // only occurs when the asynchronous dispatcher is shutting down, typically
  // just prior to process exit.
  auto status = g_event.wait_one(SIGNAL_CONTEXT_RELEASED,
                                 zx::deadline_after(kSynchronousShutdownTimeout), nullptr);
  if (status == ZX_OK) {
    handle_context_released();
    return;
  }

  // Uh oh.
  auto context_refs = g_context_refs.load(std::memory_order_relaxed);
  fprintf(stderr,
          "TraceEngine: Timed out waiting for %u buffer, %u prolonged trace context\n"
          "references (raw 0x%x) to be released after %lu ns\n"
          "while the asynchronous dispatcher was shutting down.\n"
          "Tracing will no longer be available in this process.",
          get_buffer_context_refs(context_refs), get_prolonged_context_refs(context_refs),
          context_refs, kSynchronousShutdownTimeout.get());
}

void handle_event(async_dispatcher_t* dispatcher, async_wait_t* wait, zx_status_t status,
                  const zx_packet_signal_t* signal) {
  // Note: This function may get all signals at the same time.

  if (status == ZX_OK) {
    if (signal->observed & SIGNAL_ALL_OBSERVERS_STARTED) {
      handle_all_observers_started();
    }
    if (signal->observed & SIGNAL_CONTEXT_RELEASED) {
      handle_context_released();
      return;  // trace engine is completely stopped now
    }
    status = async_begin_wait(dispatcher, &g_event_wait);
  }

  if (status != ZX_OK) {
    handle_hard_shutdown(dispatcher);
  }
}

}  // namespace

/*** Trace instrumentation functions ***/

// thread-safe, lock-free
EXPORT trace_state_t trace_state() {
  return static_cast<trace_state_t>(g_state.load(std::memory_order_relaxed));
}

// thread-safe
EXPORT bool trace_is_category_enabled(const char* category_literal) {
  trace_context_t* context = trace_acquire_context();
  if (likely(!context))
    return false;
  bool result = trace_context_is_category_enabled(context, category_literal);
  trace_release_context(context);
  return result;
}

// thread-safe, fail-fast, lock-free
EXPORT trace_context_t* trace_acquire_context() {
  // Fail fast: Check whether we could possibly write into the trace buffer.
  // The count must be at least 1 to indicate that the buffer is initialized.
  // This is marked likely because tracing is usually disabled and we want
  // to return as quickly as possible from this function.
  uint32_t count = g_context_refs.load(std::memory_order_relaxed);
  if (likely(count == 0u))
    return nullptr;

  // Attempt to increment the reference count.
  // This also acts as a fence for future access to buffer state variables.
  //
  // Note the ACQUIRE fence here since the trace context may have changed
  // from the perspective of this thread.
  while (!g_context_refs.compare_exchange_weak(count, count + kBufferCounterIncrement,
                                               std::memory_order_acquire,
                                               std::memory_order_relaxed)) {
    if (unlikely(count == 0u))
      return nullptr;
  }
  return g_context;
}

// thread-safe, fail-fast, lock-free
EXPORT trace_context_t* trace_acquire_context_for_category(const char* category_literal,
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

// TODO(fxbug.dev/22947): This function is split out from
// |trace_acquire_context_for_category_cached()| because gcc doesn't
// optimize the prologue as well as it could: It creates the stack frame
// for the entire function prior to the "is disabled?" early-exit test.
// Clang does fine, but for now to achieve optimum performance for the common
// case of tracing off, regardless of compiler, we employ this workaround.
// Both gcc and clang do the expected tail-call optimization, so all this
// costs is an extra branch when tracing is on.
//
// |current_state| is appended as an argument, violating the convention to
// put output parameters last to minimize the changes in the caller's tail
// call.
static __NO_INLINE trace_context_t* trace_acquire_context_for_category_cached_worker(
    const char* category_literal, trace_site_t* site, trace_string_ref_t* out_ref,
    trace_site_state_t current_state) {
  trace_context_t* context = trace_acquire_context_for_category(category_literal, out_ref);

  if (likely((current_state & kSiteStateFlagsMask) != kSiteStateUnknown)) {
    return context;
  }

  // First time through for this trace run. Note that multiple threads may
  // get to this point for the same call-site.
  add_to_site_cache(site, current_state, context != nullptr);

  return context;
}

// thread-safe, fail-fast, lock-free
EXPORT trace_context_t* trace_acquire_context_for_category_cached(const char* category_literal,
                                                                  trace_site_t* site,
                                                                  trace_string_ref_t* out_ref) {
  trace_site_atomic_state_t* state_ptr = get_trace_site_state_as_atomic(site);

  trace_site_state_t current_state = state_ptr->load(std::memory_order_relaxed);
  if (likely(current_state & kSiteStateDisabled)) {
    return nullptr;
  }

  return trace_acquire_context_for_category_cached_worker(category_literal, site, out_ref,
                                                          current_state);
}

// thread-safe
EXPORT zx_status_t trace_engine_flush_category_cache(void) {
  std::lock_guard<std::mutex> lock(g_engine_mutex);

  if (g_state.load(std::memory_order_relaxed) != TRACE_STOPPED)
    return ZX_ERR_BAD_STATE;

  // Empty the site cache. The next time the app tries to emit a trace event
  // it will get re-added to the cache, but that's ok.
  flush_site_cache();

  return ZX_OK;
}

// thread-safe, never-fail, lock-free
EXPORT void trace_release_context(trace_context_t* context) {
  ZX_DEBUG_ASSERT(context == g_context);
  ZX_DEBUG_ASSERT(get_buffer_context_refs(g_context_refs.load(std::memory_order_relaxed)) != 0u);

  // Note the RELEASE fence here since the trace context and trace buffer
  // contents may have changes from the perspective of other threads.
  auto previous = g_context_refs.fetch_sub(kBufferCounterIncrement, std::memory_order_release);
  if (unlikely(previous == kBufferCounterIncrement)) {
    // Notify the engine that the last reference was released.
    zx_status_t status = g_event.signal(0u, SIGNAL_CONTEXT_RELEASED);
    ZX_DEBUG_ASSERT(status == ZX_OK);
  }
}

// thread-safe, fail-fast, lock-free
EXPORT_NO_DDK trace_prolonged_context_t* trace_acquire_prolonged_context() {
  // There's no need for extreme efficiency here, but for consistency with
  // |trace_acquire_context()| we copy what it does.
  uint32_t count = g_context_refs.load(std::memory_order_relaxed);
  if (likely(count == 0u))
    return nullptr;

  // Attempt to increment the reference count.
  // This also acts as a fence for future access to buffer state variables.
  //
  // Note the ACQUIRE fence here since the trace context may have changed
  // from the perspective of this thread.
  while (!g_context_refs.compare_exchange_weak(count, count + kProlongedCounterIncrement,
                                               std::memory_order_acquire,
                                               std::memory_order_relaxed)) {
    if (likely(count == 0u))
      return nullptr;
  }
  ZX_DEBUG_ASSERT(get_prolonged_context_refs(g_context_refs.load(std::memory_order_relaxed)) <=
                  kMaxProlongedCounter);
  return reinterpret_cast<trace_prolonged_context_t*>(g_context);
}

// thread-safe, never-fail, lock-free
EXPORT_NO_DDK void trace_release_prolonged_context(trace_prolonged_context_t* context) {
  auto tcontext = reinterpret_cast<trace_context_t*>(context);
  ZX_DEBUG_ASSERT(tcontext == g_context);
  ZX_DEBUG_ASSERT(get_prolonged_context_refs(g_context_refs.load(std::memory_order_relaxed)) != 0u);

  // Note the RELEASE fence here since the trace context and trace buffer
  // contents may have changes from the perspective of other threads.
  auto previous = g_context_refs.fetch_sub(kProlongedCounterIncrement, std::memory_order_release);
  if (unlikely(previous == kProlongedCounterIncrement)) {
    // Notify the engine that the last reference was released.
    zx_status_t status = g_event.signal(0u, SIGNAL_CONTEXT_RELEASED);
    ZX_DEBUG_ASSERT(status == ZX_OK);
  }
}

/*** Asynchronous observers ***/

EXPORT zx_status_t trace_register_observer(zx_handle_t event) {
  std::lock_guard<std::mutex> lock(g_engine_mutex);

  for (const auto& item : g_observers) {
    if (item.event == event)
      return ZX_ERR_INVALID_ARGS;
  }

  g_observers.push_back(Observer{event, false});
  return ZX_OK;
}

EXPORT zx_status_t trace_unregister_observer(zx_handle_t event) {
  std::lock_guard<std::mutex> lock(g_engine_mutex);

  for (auto it = g_observers.begin(); it != g_observers.end(); ++it) {
    if (it->event == event) {
      bool awaited = it->awaiting_update_after_start;
      g_observers.erase(it);
      if (awaited) {
        notify_engine_all_observers_started_if_needed_locked();
      }
      return ZX_OK;
    }
  }
  return ZX_ERR_NOT_FOUND;
}

EXPORT void trace_notify_observer_updated(zx_handle_t event) {
  std::lock_guard<std::mutex> lock(g_engine_mutex);

  for (auto& item : g_observers) {
    if (item.event == event) {
      if (item.awaiting_update_after_start) {
        item.awaiting_update_after_start = false;
        notify_engine_all_observers_started_if_needed_locked();
      }
      return;
    }
  }
}
