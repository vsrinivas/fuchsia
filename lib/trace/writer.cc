// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace/writer.h"

#include <atomic>

#include "apps/tracing/lib/trace/internal/trace_engine.h"
#include "lib/ftl/logging.h"

using namespace ::tracing::internal;

namespace tracing {
namespace internal {
namespace {

// Unfortunately, we can't use ordinary ftl::RefPtrs or std::shared_ptrs
// since the smart pointers themselves are not atomic.  For performance
// reasons, we need to be able to atomically acquire an engine reference
// without grabbing a mutex except when changing states.

std::mutex g_mutex;

// The current state of tracing.
enum class State {
  // No tracing happening.
  kStopped,
  // Tracing has started.
  kStarted,
  // Tracing has been stopped by client.
  kStopping,
  // Tracing has finished as far as the trace engine is concerned but we
  // are waiting release of all handles before cleaning up.
  kAwaitingRelease
};
State g_state = State::kStopped;  // guarded by g_mutex

// The owner of the engine which keeps it alive until all refs released.
std::unique_ptr<TraceEngine> g_owned_engine;  // guarded by g_mutex

// Pending finish callback state.
writer::TraceFinishedCallback g_finished_callback;  // guarded by g_mutex
writer::TraceDisposition g_trace_disposition;       // guarded by g_mutex

// The currently active trace engine.
// - Setting and clearing the active engine only happens with the mutex held.
// - Accessing the active engine may happen without holding the mutex but
//   only after incrementing the reference count to acquire it.
std::atomic<TraceEngine*> g_active_engine{nullptr};
std::atomic<uint32_t> g_active_refs{0u};

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

}  // namespace

// Releases a reference to the engine, decrementing the reference count
// and finishing tracing when the count hits zero.
void ReleaseEngine() {
  if (g_active_refs.fetch_sub(1u, std::memory_order_acq_rel) != 1u)
    return;

  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_state != State::kAwaitingRelease)
    return;

  // Post the finished callback and destroy the engine.
  FTL_DCHECK(g_active_engine.load(std::memory_order_relaxed) == nullptr);
  FTL_DCHECK(g_owned_engine);
  g_owned_engine->task_runner()->PostTask([
    finished_callback = std::move(g_finished_callback),
    trace_disposition = g_trace_disposition
  ] { finished_callback(trace_disposition); });
  g_owned_engine.reset();
  g_state = State::kStopped;
}

}  // namespace internal

namespace writer {

bool StartTracing(mx::vmo buffer,
                  mx::eventpair fence,
                  std::vector<std::string> enabled_categories,
                  TraceFinishedCallback finished_callback) {
  FTL_DCHECK(buffer);
  FTL_DCHECK(fence);

  std::lock_guard<std::mutex> lock(g_mutex);
  FTL_CHECK(g_state == State::kStopped);

  // Start the engine.
  g_owned_engine = TraceEngine::Create(std::move(buffer), std::move(fence),
                                       std::move(enabled_categories));
  if (!g_owned_engine)
    return false;

  g_owned_engine->StartTracing([](TraceDisposition disposition) {
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      FTL_CHECK(g_state == State::kStarted || g_state == State::kStopping);

      // Clear the engine so no new references to it will be taken.
      if (g_state == State::kStarted)
        g_active_engine.store(nullptr, std::memory_order_relaxed);

      // Wait for the last reference to the engine to be released before
      // delivering the finished callback to the client.
      g_trace_disposition = disposition;
      g_state = State::kAwaitingRelease;
    }

    // Release initial reference.
    ReleaseEngine();
  });
  g_finished_callback = std::move(finished_callback);
  g_state = State::kStarted;

  // Acquire the initial reference to the engine.
  g_active_refs.fetch_add(1u, std::memory_order_acq_rel);
  g_active_engine.store(g_owned_engine.get(), std::memory_order_relaxed);
  return true;
}

void StopTracing() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_state != State::kStarted)
    return;

  // Clear the engine so no new references to it will be taken.
  // When the engine has stopped, it will invoke the finished callback.
  g_active_engine.store(nullptr, std::memory_order_relaxed);
  g_owned_engine->StopTracing();
  g_state = State::kStopping;
}

bool IsTracingEnabled() {
  return g_active_engine.load(std::memory_order_relaxed) != nullptr;
}

bool IsTracingEnabledForCategory(const char* category) {
  TraceEngine* engine = AcquireEngine();
  if (!engine)
    return false;
  bool result = engine->IsCategoryEnabled(category);
  ReleaseEngine();
  return result;
}

TraceWriter TraceWriter::Prepare() {
  return TraceWriter(AcquireEngine());
}

StringRef TraceWriter::RegisterString(const char* constant) {
  FTL_DCHECK(engine_);
  return engine_->RegisterString(constant, false);
}

ThreadRef TraceWriter::RegisterCurrentThread() {
  FTL_DCHECK(engine_);
  return engine_->RegisterCurrentThread();
}

void TraceWriter::WriteInitializationRecord(uint64_t ticks_per_second) {
  FTL_DCHECK(engine_);
  engine_->WriteInitializationRecord(ticks_per_second);
}

void TraceWriter::WriteStringRecord(StringIndex index, const char* value) {
  FTL_DCHECK(engine_);
  engine_->WriteStringRecord(index, value);
}

void TraceWriter::WriteThreadRecord(ThreadIndex index,
                                    mx_koid_t process_koid,
                                    mx_koid_t thread_koid) {
  FTL_DCHECK(engine_);
  engine_->WriteThreadRecord(index, process_koid, thread_koid);
}

Payload TraceWriter::WriteKernelObjectRecordBase(mx_handle_t handle,
                                                 size_t argument_count,
                                                 size_t payload_size) {
  FTL_DCHECK(engine_);
  return engine_->WriteKernelObjectRecordBase(handle, argument_count,
                                              payload_size);
}

CategorizedTraceWriter CategorizedTraceWriter::Prepare(
    const char* category_constant) {
  TraceEngine* engine = AcquireEngine();
  if (engine) {
    StringRef category_ref = engine->RegisterString(category_constant, true);
    if (!category_ref.is_empty())
      return CategorizedTraceWriter(engine, category_ref);
    ReleaseEngine();
  }
  return CategorizedTraceWriter(nullptr, StringRef::MakeEmpty());
}

Payload CategorizedTraceWriter::WriteEventRecordBase(EventType type,
                                                     const char* name,
                                                     size_t argument_count,
                                                     size_t payload_size) {
  FTL_DCHECK(engine_);
  return engine_->WriteEventRecordBase(type, category_ref_, name,
                                       argument_count, payload_size);
}

}  // namespace writer
}  // namespace tracing
