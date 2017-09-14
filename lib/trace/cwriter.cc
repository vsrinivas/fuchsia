// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace/writer.h"

#include "apps/tracing/lib/trace/internal/cevent_helpers.h"
#include "apps/tracing/lib/trace/internal/trace_engine.h"

#include "lib/fxl/logging.h"

using EventType = tracing::EventType;
using Payload = tracing::writer::Payload;
using StringRef = tracing::writer::StringRef;
using ThreadRef = tracing::writer::ThreadRef;
using TraceEngine = tracing::internal::TraceEngine;

bool ctrace_is_enabled() {
  return tracing::writer::IsTracingEnabled();
}

bool ctrace_category_is_enabled(const char* category) {
  return tracing::writer::IsTracingEnabledForCategory(category);
}

// There's nothing in a writer besides the engine. Thus we can simplify
// things by just returning a pointer to the engine here. Doing things this
// way involves converting writers to engines and back, but it keeps knowledge
// of the existence of TraceEngine out of the C API.
// Note that while the C++ doesn't do this, it can keep TraceEngine private
// so it doesn't need to.
static inline TraceEngine* ToEngine(ctrace_writer_t* writer) {
  return reinterpret_cast<TraceEngine*>(writer);
}

ctrace_writer_t* ctrace_writer_acquire() {
  return reinterpret_cast<ctrace_writer_t*>(tracing::internal::AcquireEngine());
}

void ctrace_writer_release(ctrace_writer_t* writer) {
  tracing::internal::ReleaseEngine();
}

void ctrace_register_current_thread(ctrace_writer_t* writer,
                                    ctrace_threadref_t* out_ref) {
  FXL_DCHECK(writer);
  auto engine = ToEngine(writer);
  ThreadRef tr(engine->RegisterCurrentThread());
  *out_ref = tr.c_ref();
}

bool ctrace_register_category_string(
    ctrace_writer_t* writer,
    const char* string,
    bool check_category,
    ctrace_stringref_t* out_ref) {
  FXL_DCHECK(writer);
  auto engine = ToEngine(writer);
  StringRef sr(engine->RegisterString(string, check_category));
  if (check_category && sr.is_empty())
    return false;
  *out_ref = sr.c_ref();
  return true;
}

void ctrace_register_string(
    ctrace_writer_t* writer,
    const char* string,
    ctrace_stringref_t* out_ref) {
  bool success = ctrace_register_category_string(writer, string, false, out_ref);
  FXL_DCHECK(success);
}

namespace tracing {
namespace writer {

class ArgListWriter final {
 public:
  // |c_args| must outlive this object.
  ArgListWriter(ctrace_writer_t* writer, const ctrace_arglist_t* c_args);
  size_t ElementCount() const { return c_args_->n_args; }
  size_t Size() const;
  void WriteTo(Payload& payload) const;

 private:
  ctrace_writer_t* writer_;
  const ctrace_arglist_t* c_args_;
  size_t size_;

  // As an optimization, we remember the stringrefs of each arg's name.
  // This saves having to re-register the string in WriteTo().
  ctrace_stringref_t name_crefs_[CTRACE_MAX_ARGS];

  FXL_DISALLOW_COPY_AND_ASSIGN(ArgListWriter);
};

ArgListWriter::ArgListWriter(ctrace_writer_t* writer,
                             const ctrace_arglist_t* c_args)
    : writer_(writer), c_args_(c_args) {
  size_ = 0u;
  for (size_t i = 0; i < c_args_->n_args; ++i) {
    const ctrace_argspec_t* carg = &c_args->args[i];
    ctrace_register_string(writer, carg->name, &name_crefs_[i]);
    StringRef name_ref(name_crefs_[i]);
    switch (carg->type) {
      case CTRACE_ARGUMENT_NULL: {
        NullArgument arg(name_ref);
        size_ += arg.Size();
        break;
      }
      case CTRACE_ARGUMENT_INT32: {
        Int32Argument arg(name_ref, carg->u.i32);
        size_ += arg.Size();
        break;
      }
      case CTRACE_ARGUMENT_UINT32: {
        Uint32Argument arg(name_ref, carg->u.u32);
        size_ += arg.Size();
        break;
      }
      case CTRACE_ARGUMENT_INT64: {
        Int64Argument arg(name_ref, carg->u.i64);
        size_ += arg.Size();
        break;
      }
      case CTRACE_ARGUMENT_UINT64: {
        Uint64Argument arg(name_ref, carg->u.u64);
        size_ += arg.Size();
        break;
      }
      case CTRACE_ARGUMENT_DOUBLE: {
        DoubleArgument arg(name_ref, carg->u.d);
        size_ += arg.Size();
        break;
      }
      case CTRACE_ARGUMENT_STRING: {
        ctrace_stringref_t csr;
        ctrace_register_string(writer, carg->u.s, &csr);
        StringArgument arg(name_ref, StringRef(csr));
        size_ += arg.Size();
        break;
      }
      case CTRACE_ARGUMENT_POINTER: {
        PointerArgument arg(name_ref, reinterpret_cast<uintptr_t>(carg->u.p));
        size_ += arg.Size();
        break;
      }
      case CTRACE_ARGUMENT_KOID: {
        KoidArgument arg(name_ref, carg->u.koid);
        size_ += arg.Size();
        break;
      }
      default:
        FXL_NOTREACHED();
    }
  }
}

size_t ArgListWriter::Size() const {
  return size_;
}

void ArgListWriter::WriteTo(Payload& payload) const {
  for (size_t i = 0; i < c_args_->n_args; ++i) {
    const ctrace_argspec_t* carg = &c_args_->args[i];
    StringRef name_ref(name_crefs_[i]);
    switch (carg->type) {
      case CTRACE_ARGUMENT_NULL: {
        NullArgument arg(name_ref);
        arg.WriteTo(payload);
        break;
      }
      case CTRACE_ARGUMENT_INT32: {
        Int32Argument arg(name_ref, carg->u.i32);
        arg.WriteTo(payload);
        break;
      }
      case CTRACE_ARGUMENT_UINT32: {
        Uint32Argument arg(name_ref, carg->u.u32);
        arg.WriteTo(payload);
        break;
      }
      case CTRACE_ARGUMENT_INT64: {
        Int64Argument arg(name_ref, carg->u.i64);
        arg.WriteTo(payload);
        break;
      }
      case CTRACE_ARGUMENT_UINT64: {
        Uint64Argument arg(name_ref, carg->u.u64);
        arg.WriteTo(payload);
        break;
      }
      case CTRACE_ARGUMENT_DOUBLE: {
        DoubleArgument arg(name_ref, carg->u.d);
        arg.WriteTo(payload);
        break;
      }
      case CTRACE_ARGUMENT_STRING: {
        ctrace_stringref_t csr;
        ctrace_register_string(writer_, carg->u.s, &csr);
        StringArgument arg(name_ref, StringRef(csr));
        arg.WriteTo(payload);
        break;
      }
      case CTRACE_ARGUMENT_POINTER: {
        PointerArgument arg(name_ref, reinterpret_cast<uintptr_t>(carg->u.p));
        arg.WriteTo(payload);
        break;
      }
      case CTRACE_ARGUMENT_KOID: {
        KoidArgument arg(name_ref, carg->u.koid);
        arg.WriteTo(payload);
        break;
      }
      default:
        FXL_NOTREACHED();
    }
  }
}

}  // namespace writer
}  // namespace tracing

using ArgListWriter = tracing::writer::ArgListWriter;

namespace {
const ctrace_arglist_t empty_arg_list = {};
}

static inline void WriteEventRecord0(
    ctrace_writer_t* writer,
    uint64_t event_time,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    const ctrace_arglist_t* c_args,
    EventType event_type) {
  FXL_DCHECK(writer);
  auto engine = ToEngine(writer);
  if (!c_args)
    c_args = &empty_arg_list;
  ArgListWriter args(writer, c_args);
  if (Payload payload = engine->WriteEventRecordBase(
        event_type, event_time, ThreadRef(*thread_ref),
        StringRef(*category_ref), StringRef(*name_ref),
        args.ElementCount(), args.Size())) {
    payload.WriteValue(args);
  }
}

static inline void WriteEventRecord1(
    ctrace_writer_t* writer,
    uint64_t event_time,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    const ctrace_arglist_t* c_args,
    EventType event_type,
    uint64_t extra_arg) {
  FXL_DCHECK(writer);
  auto engine = ToEngine(writer);
  if (!c_args)
    c_args = &empty_arg_list;
  ArgListWriter args(writer, c_args);
  if (Payload payload = engine->WriteEventRecordBase(
        event_type, event_time, ThreadRef(*thread_ref),
        StringRef(*category_ref), StringRef(*name_ref),
        args.ElementCount(), args.Size() + sizeof(uint64_t))) {
    payload.WriteValue(args).Write(extra_arg);
  }
}

void ctrace_write_instant_event_record(
    ctrace_writer_t* writer,
    uint64_t event_time,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    ctrace_scope_t scope,
    const ctrace_arglist_t* c_args) {
  WriteEventRecord1(writer, event_time,
                    thread_ref, category_ref, name_ref, c_args,
                    EventType::kInstant, scope);
}

void ctrace_internal_write_instant_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    ctrace_scope_t scope,
    const ctrace_arglist_t* args) {
  ctrace_threadref_t current_thread;
  ctrace_stringref_t name_ref;
  ctrace_register_current_thread(writer, &current_thread);
  ctrace_register_string(writer, name, &name_ref);
  ctrace_write_instant_event_record(
      writer, zx_ticks_get(), &current_thread,
      category_ref, &name_ref, scope, args);
}

void ctrace_write_counter_event_record(
    ctrace_writer_t* writer,
    uint64_t event_time,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    uint64_t id,
    const ctrace_arglist_t* c_args) {
  WriteEventRecord1(writer, event_time,
                    thread_ref, category_ref, name_ref, c_args,
                    EventType::kCounter, id);
}

void ctrace_internal_write_counter_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    uint64_t id,
    const ctrace_arglist_t* args) {
  ctrace_threadref_t current_thread;
  ctrace_stringref_t name_ref;
  ctrace_register_current_thread(writer, &current_thread);
  ctrace_register_string(writer, name, &name_ref);
  ctrace_write_counter_event_record(
      writer, zx_ticks_get(), &current_thread,
      category_ref, &name_ref, id, args);
}

void ctrace_write_duration_event_record(
    ctrace_writer_t* writer,
    uint64_t start_time,
    uint64_t end_time,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    const ctrace_arglist_t* c_args) {
  // See TraceWriter::WriteDurationEventRecord.
  ctrace_write_duration_begin_event_record(writer, start_time, thread_ref,
                                           category_ref, name_ref, c_args);
  ctrace_arglist_t empty_args = {0, nullptr};
  ctrace_write_duration_end_event_record(writer, end_time, thread_ref,
                                         category_ref, name_ref, &empty_args);
}

void ctrace_write_duration_begin_event_record(
    ctrace_writer_t* writer,
    uint64_t event_time,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    const ctrace_arglist_t* c_args) {
  WriteEventRecord0(writer, event_time,
                    thread_ref, category_ref, name_ref, c_args,
                    EventType::kDurationBegin);
}

void ctrace_internal_write_duration_begin_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    const ctrace_arglist_t* args) {
  ctrace_threadref_t current_thread;
  ctrace_stringref_t name_ref;
  ctrace_register_current_thread(writer, &current_thread);
  ctrace_register_string(writer, name, &name_ref);
  ctrace_write_duration_begin_event_record(
      writer, zx_ticks_get(), &current_thread,
      category_ref, &name_ref, args);
}

void ctrace_write_duration_end_event_record(
    ctrace_writer_t* writer,
    uint64_t event_time,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    const ctrace_arglist_t* c_args) {
  WriteEventRecord0(writer, event_time,
                    thread_ref, category_ref, name_ref, c_args,
                    EventType::kDurationEnd);
}

void ctrace_internal_write_duration_end_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    const ctrace_arglist_t* args) {
  ctrace_threadref_t current_thread;
  ctrace_stringref_t name_ref;
  ctrace_register_current_thread(writer, &current_thread);
  ctrace_register_string(writer, name, &name_ref);
  ctrace_write_duration_end_event_record(
      writer, zx_ticks_get(), &current_thread,
      category_ref, &name_ref, args);
}

void ctrace_write_async_begin_event_record(
    ctrace_writer_t* writer,
    uint64_t event_time,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    uint64_t id,
    const ctrace_arglist_t* c_args) {
  WriteEventRecord1(writer, event_time,
                    thread_ref, category_ref, name_ref, c_args,
                    EventType::kAsyncStart, id);
}

void ctrace_internal_write_async_begin_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    uint64_t id,
    const ctrace_arglist_t* args) {
  ctrace_threadref_t current_thread;
  ctrace_stringref_t name_ref;
  ctrace_register_current_thread(writer, &current_thread);
  ctrace_register_string(writer, name, &name_ref);
  ctrace_write_async_begin_event_record(
      writer, zx_ticks_get(), &current_thread,
      category_ref, &name_ref, id, args);
}

void ctrace_write_async_instant_event_record(
    ctrace_writer_t* writer,
    uint64_t event_time,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    uint64_t id,
    const ctrace_arglist_t* c_args) {
  WriteEventRecord1(writer, event_time,
                    thread_ref, category_ref, name_ref, c_args,
                    EventType::kAsyncInstant, id);
}

void ctrace_internal_write_async_instant_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    uint64_t id,
    const ctrace_arglist_t* args) {
  ctrace_threadref_t current_thread;
  ctrace_stringref_t name_ref;
  ctrace_register_current_thread(writer, &current_thread);
  ctrace_register_string(writer, name, &name_ref);
  ctrace_write_async_instant_event_record(
      writer, zx_ticks_get(), &current_thread,
      category_ref, &name_ref, id, args);
}

void ctrace_write_async_end_event_record(
    ctrace_writer_t* writer,
    uint64_t event_time,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    uint64_t id,
    const ctrace_arglist_t* c_args) {
  WriteEventRecord1(writer, event_time,
                    thread_ref, category_ref, name_ref, c_args,
                    EventType::kAsyncEnd, id);
}

void ctrace_internal_write_async_end_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    uint64_t id,
    const ctrace_arglist_t* args) {
  ctrace_threadref_t current_thread;
  ctrace_stringref_t name_ref;
  ctrace_register_current_thread(writer, &current_thread);
  ctrace_register_string(writer, name, &name_ref);
  ctrace_write_async_end_event_record(
      writer, zx_ticks_get(), &current_thread,
      category_ref, &name_ref, id, args);
}

void ctrace_write_flow_begin_event_record(
    ctrace_writer_t* writer,
    uint64_t event_time,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    uint64_t id,
    const ctrace_arglist_t* c_args) {
  WriteEventRecord1(writer, event_time,
                    thread_ref, category_ref, name_ref, c_args,
                    EventType::kFlowBegin, id);
}

void ctrace_internal_write_flow_begin_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    uint64_t id,
    const ctrace_arglist_t* args) {
  ctrace_threadref_t current_thread;
  ctrace_stringref_t name_ref;
  ctrace_register_current_thread(writer, &current_thread);
  ctrace_register_string(writer, name, &name_ref);
  ctrace_write_flow_begin_event_record(
      writer, zx_ticks_get(), &current_thread,
      category_ref, &name_ref, id, args);
}

void ctrace_write_flow_step_event_record(
    ctrace_writer_t* writer,
    uint64_t event_time,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    uint64_t id,
    const ctrace_arglist_t* c_args) {
  WriteEventRecord1(writer, event_time,
                    thread_ref, category_ref, name_ref, c_args,
                    EventType::kFlowStep, id);
}

void ctrace_internal_write_flow_step_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    uint64_t id,
    const ctrace_arglist_t* args) {
  ctrace_threadref_t current_thread;
  ctrace_stringref_t name_ref;
  ctrace_register_current_thread(writer, &current_thread);
  ctrace_register_string(writer, name, &name_ref);
  ctrace_write_flow_step_event_record(
      writer, zx_ticks_get(), &current_thread,
      category_ref, &name_ref, id, args);
}

void ctrace_write_flow_end_event_record(
    ctrace_writer_t* writer,
    uint64_t event_time,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    uint64_t id,
    const ctrace_arglist_t* c_args) {
  WriteEventRecord1(writer, event_time,
                    thread_ref, category_ref, name_ref, c_args,
                    EventType::kFlowEnd, id);
}

void ctrace_internal_write_flow_end_event_record(
    ctrace_writer_t* writer,
    const ctrace_stringref_t* category_ref,
    const char* name,
    uint64_t id,
    const ctrace_arglist_t* args) {
  ctrace_threadref_t current_thread;
  ctrace_stringref_t name_ref;
  ctrace_register_current_thread(writer, &current_thread);
  ctrace_register_string(writer, name, &name_ref);
  ctrace_write_flow_end_event_record(
      writer, zx_ticks_get(), &current_thread,
      category_ref, &name_ref, id, args);
}

void ctrace_write_kernel_object_record(
    ctrace_writer_t* writer,
    zx_handle_t handle,
    const ctrace_arglist_t* c_args) {
  FXL_DCHECK(writer);
  auto engine = ToEngine(writer);
  ArgListWriter args(writer, c_args);
  if (Payload payload = engine->WriteKernelObjectRecordBase(
        handle, args.ElementCount(), args.Size())) {
    payload.WriteValue(args);
  }
}

void ctrace_internal_write_kernel_object_record(
    ctrace_writer_t* writer,
    zx_handle_t handle,
    const ctrace_arglist_t* args) {
  ctrace_write_kernel_object_record(writer, handle, args);
}
