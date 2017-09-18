// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// C interface to the writer.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zircon/compiler.h>
#include <zircon/types.h>

#include "garnet/lib/trace/ctypes.h"

__BEGIN_CDECLS

// Returns true if tracing is active.
//
// This method is thread-safe.
bool ctrace_is_enabled(void);

// Returns true if the tracer has been initialized by a call to |StartTracing|
// and the specified |category| has been enabled.
//
// This method is thread-safe.
bool ctrace_category_is_enabled(const char* category);

// The list of args is constructed as an array of these.
typedef struct {
  ctrace_argument_t type;
  const char* name;
  union {
    int32_t i32;
    uint32_t u32;
    int64_t i64;
    uint64_t u64;
    double d;
    const char* s;
    const void* p;
    uint64_t koid;
  } u;
} ctrace_argspec_t;

typedef struct {
  size_t n_args;
  const ctrace_argspec_t* args;
} ctrace_arglist_t;

// The trace format specified maximum number of args.
#define CTRACE_MAX_ARGS (15u)

// ctrace_writer_t is opaque to the API
typedef struct ctrace_writer ctrace_writer_t;

// A thread reference which is either encoded inline or indirectly by
// thread table index.
typedef struct ctrace_threadref {
  ctrace_encoded_threadref_t encoded_value;
  zx_koid_t inline_process_koid;
  zx_koid_t inline_thread_koid;
} ctrace_threadref_t;

// A string reference which is either encoded inline or indirectly by
// string table index.
typedef struct ctrace_stringref {
  ctrace_encoded_stringref_t encoded_value;
  const char* inline_string;
} ctrace_stringref_t;

ctrace_writer_t* ctrace_writer_acquire(void);

// This must be called before the result of ctrace_writer_acquire goes
// out of scope.
// TODO(dje): Provide attribute cleanup support?
void ctrace_writer_release(ctrace_writer_t*);

// Registers the current thread into the thread table and automatically
// writes its description.  Automatically writes a description of the
// current process and thread when first used.
void ctrace_register_current_thread(ctrace_writer_t* writer,
                                    ctrace_threadref_t* out_ref);

// Registers the string into the string table without copying its contents.
// The |constant| is not copied; it must outlive the trace session.
// If |check_category| is true, returns false if the string is not one of
// the enabled categories and leaves |*out_ref| unmodified.
// Otherwise returns true.
bool ctrace_register_category_string(
    ctrace_writer_t* writer,
    const char* category,
    bool check_category,
    ctrace_stringref_t* out_ref);

// Registers the string into the string table without copying its contents.
// The |string| is not copied; it must outlive the trace session.
void ctrace_register_string(
    ctrace_writer_t* writer,
    const char* string,
    ctrace_stringref_t* out_ref);

// These ctrace_write_* functions are the C API equivalent of the
// TraceWriter::Write*EventRecord methods.

void ctrace_write_instant_event_record(
    ctrace_writer_t* writer,
    uint64_t event_time,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    ctrace_scope_t scope,
    const ctrace_arglist_t* args);

void ctrace_write_counter_event_record(
    ctrace_writer_t* writer,
    uint64_t event_time,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    uint64_t id,
    const ctrace_arglist_t* args);

void ctrace_write_duration_event_record(
    ctrace_writer_t* writer,
    uint64_t start_time,
    uint64_t end_time,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    const ctrace_arglist_t* args);

void ctrace_write_duration_begin_event_record(
    ctrace_writer_t* writer,
    uint64_t event_time,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    const ctrace_arglist_t* args);

void ctrace_write_duration_end_event_record(
    ctrace_writer_t* writer,
    uint64_t event_time,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    const ctrace_arglist_t* args);

void ctrace_write_async_begin_event_record(
    ctrace_writer_t* writer,
    uint64_t event_time,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    uint64_t id,
    const ctrace_arglist_t* args);

void ctrace_write_async_instant_event_record(
    ctrace_writer_t* writer,
    uint64_t event_time,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    uint64_t id,
    const ctrace_arglist_t* args);

void ctrace_write_async_end_event_record(
    ctrace_writer_t* writer,
    uint64_t event_time,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    uint64_t id,
    const ctrace_arglist_t* args);

void ctrace_write_flow_begin_event_record(
    ctrace_writer_t* writer,
    uint64_t event_time,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    uint64_t id,
    const ctrace_arglist_t* args);

void ctrace_write_flow_step_event_record(
    ctrace_writer_t* writer,
    uint64_t event_time,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    uint64_t id,
    const ctrace_arglist_t* args);

void ctrace_write_flow_end_event_record(
    ctrace_writer_t* writer,
    uint64_t event_time,
    const ctrace_threadref_t* thread_ref,
    const ctrace_stringref_t* category_ref,
    const ctrace_stringref_t* name_ref,
    uint64_t id,
    const ctrace_arglist_t* args);

void ctrace_write_kernel_object_record(
    ctrace_writer_t* writer,
    zx_handle_t handle,
    const ctrace_arglist_t* args);

__END_CDECLS
