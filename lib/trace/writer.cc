// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace/writer.h"

#include <memory>

#include "apps/tracing/lib/trace/internal/trace_engine.h"
#include "lib/ftl/logging.h"

using namespace ::tracing::internal;

namespace tracing {
namespace writer {
namespace {

std::unique_ptr<TraceEngine> g_engine;

}  // namespace

void StartTracing(mx::vmo current,
                  mx::vmo next,
                  std::vector<std::string> enabled_categories) {
  FTL_DCHECK(!g_engine);

  FTL_VLOG(1) << "Started tracing...";
  g_engine =
      TraceEngine::Create(std::move(current), std::move(enabled_categories));
}

void StopTracing() {
  FTL_VLOG(1) << "Stopped tracing...";

  // TODO(jeffbrown): Don't leak this.
  // Keep track of active trace writers to prevent race conditions on shutdown.
  // Can we do this without introducing gratuitous ref counting or atomic ops?
  g_engine.release();
}

bool IsTracingEnabledForCategory(const char* category) {
  TraceEngine* engine = g_engine.get();
  return engine && engine->IsCategoryEnabled(category);
}

TraceWriter::~TraceWriter() {
  // TODO(jeffbrown): Keep track of active trace writers.
  // Can we do this without introducing gratuitous ref counting or atomic ops?
}

TraceWriter TraceWriter::Prepare() {
  return TraceWriter(g_engine.get());
}

StringRef TraceWriter::RegisterString(const char* string) {
  FTL_DCHECK(engine_);
  return engine_->RegisterString(string);
}

ThreadRef TraceWriter::RegisterCurrentThread() {
  FTL_DCHECK(engine_);
  return engine_->RegisterCurrentThread();
}

void TraceWriter::WriteInitializationRecord(uint64_t ticks_per_second) {
  FTL_DCHECK(engine_);
  engine_->WriteInitializationRecord(ticks_per_second);
}

void TraceWriter::WriteStringRecord(StringIndex index, const char* string) {
  FTL_DCHECK(engine_);
  engine_->WriteStringRecord(index, string);
}

void TraceWriter::WriteThreadRecord(ThreadIndex index,
                                    uint64_t process_koid,
                                    uint64_t thread_koid) {
  FTL_DCHECK(engine_);
  engine_->WriteThreadRecord(index, process_koid, thread_koid);
}

CategorizedTraceWriter CategorizedTraceWriter::Prepare(const char* category) {
  TraceEngine* engine = g_engine.get();
  if (engine) {
    StringRef category_ref;
    if (engine->PrepareCategory(category, &category_ref))
      return CategorizedTraceWriter(engine, category_ref);
  }
  return CategorizedTraceWriter(nullptr, StringRef::MakeEmpty());
}

Payload CategorizedTraceWriter::WriteEventRecord(EventType type,
                                                 const char* name,
                                                 size_t argument_count,
                                                 size_t payload_size) {
  FTL_DCHECK(engine_);
  return engine_->WriteEventRecord(type, category_ref_, name, argument_count,
                                   payload_size);
}

}  // namespace writer
}  // namespace tracing
