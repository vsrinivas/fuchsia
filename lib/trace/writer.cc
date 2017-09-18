// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace/writer.h"

#include <atomic>
#include <unordered_map>

#include "apps/tracing/lib/trace/internal/trace_engine.h"
#include "lib/fxl/logging.h"

using namespace ::tracing::writer;
using namespace ::tracing::internal;

namespace tracing {
namespace internal {
namespace {

// Unfortunately, we can't use ordinary fxl::RefPtrs or std::shared_ptrs
// since the smart pointers themselves are not atomic.  For performance
// reasons, we need to be able to atomically acquire an engine reference
// without grabbing a mutex except when changing states.

std::mutex g_mutex;

// The current state.
// This is only ever modified on the engine thread while holding the mutex.
TraceState g_state = TraceState::kFinished;  // guarded by g_mutex
bool g_finished_posted = false;              // guarded by g_mutex

// The owner of the engine which keeps it alive until all refs released.
TraceEngine* g_owned_engine;  // guarded by g_mutex

// Pending finish callback state.
TraceFinishedCallback g_finished_callback;  // guarded by g_mutex
TraceDisposition g_trace_disposition;       // guarded by g_mutex

// The currently active trace engine.
// - Setting and clearing the active engine only happens with the mutex held.
// - Accessing the active engine may happen without holding the mutex but
//   only after incrementing the reference count to acquire it.
std::atomic<TraceEngine*> g_active_engine{nullptr};
std::atomic<uint32_t> g_active_refs{0u};

// The list of registered trace handlers.
using TraceHandlerMap = std::unordered_map<TraceHandlerKey, TraceHandler>;
TraceHandlerMap g_handlers;               // guarded by g_mutex
TraceHandlerKey g_last_handler_key = 0u;  // guarded by g_mutex

// Notifies handlers of state changes.
void NotifyHandlers(const TraceHandlerMap& handlers, TraceState state) {
  for (const auto& pair : handlers)
    pair.second(state);
}

// Called on the engine thread when tracing has completely finished.
void FinishedTracing() {
  FXL_VLOG(1) << "FinishedTracing";
  FXL_DCHECK(g_active_engine.load(std::memory_order_relaxed) == nullptr);

  TraceHandlerMap handlers;
  TraceFinishedCallback finished_callback;
  TraceDisposition trace_disposition;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    FXL_DCHECK(g_state == TraceState::kStopped);
    FXL_DCHECK(g_owned_engine);
    FXL_DCHECK(g_owned_engine->task_runner()->RunsTasksOnCurrentThread());
    FXL_DCHECK(g_finished_posted);

    g_finished_posted = false;
    g_state = TraceState::kFinished;
    delete g_owned_engine;
    g_owned_engine = nullptr;

    handlers = g_handlers;
    finished_callback = std::move(g_finished_callback);
    trace_disposition = g_trace_disposition;
  }

  NotifyHandlers(handlers, TraceState::kFinished);
  finished_callback(trace_disposition);
}

// Called on the engine thread when the engine has stopped accepting
// new trace events.
void StoppedTracing(TraceDisposition trace_disposition) {
  FXL_VLOG(1) << "StoppedTracing: trace_disposition="
              << static_cast<int>(trace_disposition);
  TraceHandlerMap handlers;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    FXL_DCHECK(g_state == TraceState::kStarted ||
               g_state == TraceState::kStopping);
    FXL_DCHECK(g_owned_engine);
    FXL_DCHECK(g_owned_engine->task_runner()->RunsTasksOnCurrentThread());

    // Clear the engine so no new references to it will be taken.
    g_active_engine.store(nullptr, std::memory_order_relaxed);

    // Wait for the last reference to the engine to be released before
    // delivering the finished callback to the client.
    g_trace_disposition = trace_disposition;
    g_state = TraceState::kStopped;
    handlers = g_handlers;
  }

  // Release initial reference to trace engine and await finished.
  NotifyHandlers(handlers, TraceState::kStopped);
  ReleaseEngine();
}

// Called when the last reference has been released.
void ReleasedLastReference() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_state == TraceState::kStopped && !g_finished_posted) {
    FXL_DCHECK(g_owned_engine);
    g_finished_posted = true;
    g_owned_engine->task_runner()->PostTask([] { FinishedTracing(); });
  }
}

}  // namespace

// Acquires a reference to the engine, incrementing the reference count.
TraceEngine* AcquireEngine() {
  // Quick check: Is there possibly an engine at all?
  TraceEngine* engine = g_active_engine.load(std::memory_order_relaxed);
  if (!engine)
    return nullptr;

  // Optimistically increment the reference count then grab the engine again.
  // We must use acquire/release to ensure that the engine load is not
  // reordered before the increment. We might get a different engine here
  // than we initially loaded but that's fine since we haven't touched it yet.
  g_active_refs.fetch_add(1u, std::memory_order_acq_rel);
  engine = g_active_engine.load(std::memory_order_relaxed);
  if (engine)
    return engine;

  // We didn't get an engine but we need to tidy up the reference count
  // as if we had.
  ReleaseEngine();
  return nullptr;
}

// Releases a reference to the engine, decrementing the reference count
// and finishing tracing when the count hits zero.
void ReleaseEngine() {
  if (g_active_refs.fetch_sub(1u, std::memory_order_acq_rel) == 1u) {
    ReleasedLastReference();
  }
}

}  // namespace internal

namespace writer {

bool StartTracing(zx::vmo buffer,
                  zx::eventpair fence,
                  std::vector<std::string> enabled_categories,
                  TraceFinishedCallback finished_callback) {
  FXL_VLOG(1) << "StartTracing";
  FXL_DCHECK(buffer);
  FXL_DCHECK(fence);

  TraceHandlerMap handlers;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    FXL_CHECK(g_state == TraceState::kFinished)
        << "Cannot start new trace session until previous session has "
           "completely finished";
    FXL_DCHECK(!g_owned_engine);
    FXL_DCHECK(!g_finished_posted);

    // Start the engine.
    auto engine = TraceEngine::Create(std::move(buffer), std::move(fence),
                                      std::move(enabled_categories));
    if (!engine)
      return false;

    g_owned_engine = engine.release();
    g_owned_engine->StartTracing(
        [](TraceDisposition disposition) { StoppedTracing(disposition); });
    g_finished_callback = std::move(finished_callback);
    g_state = TraceState::kStarted;

    // Acquire the initial reference to the engine.
    g_active_refs.fetch_add(1u, std::memory_order_acq_rel);
    g_active_engine.store(g_owned_engine, std::memory_order_relaxed);
    handlers = g_handlers;
  }

  NotifyHandlers(handlers, TraceState::kStarted);
  return true;
}

void StopTracing() {
  FXL_VLOG(1) << "StopTracing";

  // Set stopping state.
  TraceHandlerMap handlers;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_state != TraceState::kStarted)
      return;

    FXL_DCHECK(g_owned_engine);
    FXL_DCHECK(g_owned_engine->task_runner()->RunsTasksOnCurrentThread());
    g_state = TraceState::kStopping;
    handlers = g_handlers;
  }

  // Give trace handlers a shot to finish writing events before we
  // actually stop the engine.
  NotifyHandlers(handlers, TraceState::kStopping);

  // Clear the engine so no new references to it will be taken.
  // When the engine has stopped, it will invoke the stop callback.
  // It's safe to use |g_owned_engine| here because the stop and finished
  // callbacks cannot run until this function returns.
  g_active_engine.store(nullptr, std::memory_order_relaxed);
  g_owned_engine->StopTracing();
}

bool IsTracingEnabled() {
  return g_active_engine.load(std::memory_order_relaxed) != nullptr;
}

bool IsTracingEnabledForCategory(const char* category) {
  TraceEngine* engine = AcquireEngine();
  if (engine) {
    bool result = engine->IsCategoryEnabled(category);
    ReleaseEngine();
    return result;
  }
  return false;
}

TraceState GetTraceState() {
  // TODO(jeffbrown): We could make this lock-free if desired but there
  // doesn't seem to be much point.
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_state;
}

std::vector<std::string> GetEnabledCategories() {
  std::vector<std::string> result;
  TraceEngine* engine = AcquireEngine();
  if (engine) {
    result = engine->enabled_categories();
    ReleaseEngine();
  }
  return result;
}

TraceHandlerKey AddTraceHandler(TraceHandler handler) {
  std::lock_guard<std::mutex> lock(g_mutex);
  TraceHandlerKey handler_key = ++g_last_handler_key;
  g_handlers.emplace(handler_key, handler);
  return handler_key;
}

void RemoveTraceHandler(TraceHandlerKey handler_key) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_handlers.erase(handler_key);
}

TraceWriter TraceWriter::Prepare() {
  return TraceWriter(AcquireEngine());
}

StringRef TraceWriter::RegisterString(const char* constant,
                                      bool check_category) {
  FXL_DCHECK(engine_);
  return engine_->RegisterString(constant, check_category);
}

StringRef TraceWriter::RegisterStringCopy(const std::string& string) {
  FXL_DCHECK(engine_);
  return engine_->RegisterStringCopy(string);
}

ThreadRef TraceWriter::RegisterCurrentThread() {
  FXL_DCHECK(engine_);
  return engine_->RegisterCurrentThread();
}

ThreadRef TraceWriter::RegisterThread(zx_koid_t process_koid,
                                      zx_koid_t thread_koid) {
  FXL_DCHECK(engine_);
  return engine_->RegisterThread(process_koid, thread_koid);
}

void TraceWriter::WriteProcessDescription(zx_koid_t process_koid,
                                          const std::string& process_name) {
  FXL_DCHECK(engine_);
  engine_->WriteProcessDescription(process_koid, process_name);
}

void TraceWriter::WriteThreadDescription(zx_koid_t process_koid,
                                         zx_koid_t thread_koid,
                                         const std::string& thread_name) {
  FXL_DCHECK(engine_);
  engine_->WriteThreadDescription(process_koid, thread_koid, thread_name);
}

Payload TraceWriter::WriteKernelObjectRecordBase(zx_handle_t handle,
                                                 size_t argument_count,
                                                 size_t payload_size) {
  FXL_DCHECK(engine_);
  return engine_->WriteKernelObjectRecordBase(handle, argument_count,
                                              payload_size);
}

void TraceWriter::WriteContextSwitchRecord(
    Ticks event_time,
    CpuNumber cpu_number,
    ThreadState outgoing_thread_state,
    const ThreadRef& outgoing_thread_ref,
    const ThreadRef& incoming_thread_ref) {
  FXL_DCHECK(engine_);
  engine_->WriteContextSwitchRecord(event_time, cpu_number,
                                    outgoing_thread_state, outgoing_thread_ref,
                                    incoming_thread_ref);
}

void TraceWriter::WriteLogRecord(Ticks event_time,
                                 const ThreadRef& thread_ref,
                                 const char* log_message,
                                 size_t log_message_length) {
  FXL_DCHECK(engine_);
  engine_->WriteLogRecord(event_time, thread_ref, log_message,
                          log_message_length);
}

Payload TraceWriter::WriteEventRecordBase(EventType event_type,
                                          Ticks event_time,
                                          const ThreadRef& thread_ref,
                                          const StringRef& category_ref,
                                          const StringRef& name_ref,
                                          size_t argument_count,
                                          size_t payload_size) {
  FXL_DCHECK(engine_);
  return engine_->WriteEventRecordBase(event_type, event_time, thread_ref,
                                       category_ref, name_ref, argument_count,
                                       payload_size);
}

}  // namespace writer
}  // namespace tracing
